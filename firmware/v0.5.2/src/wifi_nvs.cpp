// wifi_nvs.cpp - see wifi_nvs.h.
// NVS layout (namespace "wifi"):
//   ver  u32   hash of the built-in list last copied in (from wifi_secrets.h)
//   n    u8    number of stored networks
//   s<i> str   SSID of network i      p<i> str   password of network i
//   o<i> u8    origin of network i: 0 = built-in (wifi_secrets.h), 1 = entered on the board (portal)
//   last i8    index of the last network that connected (tried first)
// Writes happen only when something changed, so normal boots do not wear the flash.
// NVS is synced BEFORE the WiFi driver starts: a flash write while the PHY is starting
// crashed the ESP32-S3 on Arduino core 3.2.0 ("Cache disabled but cached memory region accessed").
#include "wifi_nvs.h"
#include "wifi_secrets.h"
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <utility>

namespace {
constexpr uint8_t kMax = WIFI_NVS_MAX;
constexpr uint32_t kLostRetryMs = 60000;       // reconnect by scanning after 60 s offline
constexpr uint32_t kPortalRetryMs = 300000;    // while in portal, retry known networks every 5 min
constexpr uint8_t kBuiltIn = 0, kPortal = 1;

struct Cred { String ssid, pass; uint8_t origin = kBuiltIn; };

Preferences prefs;
WebServer* server = nullptr;
bool portalActive = false;
uint32_t lostSince = 0, portalLastTry = 0, timeoutMs = 10000;

void key(char* buf, char kind, uint8_t i) { snprintf(buf, 6, "%c%u", kind, i); }

uint8_t loadAll(Cred* out) {
  if (!prefs.begin("wifi", true)) return 0;     // namespace not created yet
  uint8_t n = prefs.getUChar("n", 0);
  if (n > kMax) n = kMax;
  char k[6];
  for (uint8_t i = 0; i < n; i++) {
    key(k, 's', i); out[i].ssid = prefs.getString(k, "");
    key(k, 'p', i); out[i].pass = prefs.getString(k, "");
    key(k, 'o', i); out[i].origin = prefs.getUChar(k, kBuiltIn);
  }
  prefs.end();
  return n;
}

void saveAll(const Cred* c, uint8_t n) {
  prefs.begin("wifi", false);
  uint8_t old = prefs.getUChar("n", 0);
  char k[6];
  for (uint8_t i = 0; i < n; i++) {
    key(k, 's', i); prefs.putString(k, c[i].ssid);
    key(k, 'p', i); prefs.putString(k, c[i].pass);
    key(k, 'o', i); prefs.putUChar(k, c[i].origin);
  }
  for (uint8_t i = n; i < old && i < kMax; i++) {   // drop leftovers
    key(k, 's', i); prefs.remove(k);
    key(k, 'p', i); prefs.remove(k);
    key(k, 'o', i); prefs.remove(k);
  }
  prefs.putUChar("n", n);
  prefs.end();
}

int findSsid(const Cred* c, uint8_t n, const String& s) {
  for (uint8_t i = 0; i < n; i++) if (c[i].ssid == s) return i;
  return -1;
}

// Upsert into the list; when full, the last slot is replaced. Returns the index used.
int upsert(Cred* c, uint8_t& n, const String& s, const String& p, uint8_t origin) {
  int i = findSsid(c, n, s);
  if (i < 0) { i = (n < kMax) ? n++ : kMax - 1; c[i].ssid = s; }
  c[i].pass = p;
  c[i].origin = origin;
  return i;
}

// Copy the built-in networks into NVS only when wifi_secrets.h changed.
// Built-in entries no longer in the list are dropped; networks entered on the board are kept
// (a built-in entry with the same SSID wins).
void syncDefaults() {
  uint32_t ver = 0;
  if (prefs.begin("wifi", true)) { ver = prefs.getUInt("ver", 0); prefs.end(); }
  if (ver == WIFI_SECRETS_VERSION) return;
  Cred old[kMax], list[kMax];
  uint8_t on = loadAll(old), n = 0;
  for (uint8_t i = 0; i < on; i++) if (old[i].origin == kPortal) list[n++] = old[i];
  for (size_t d = 0; d < WIFI_DEFAULT_COUNT; d++) upsert(list, n, WIFI_DEFAULTS[d].ssid, WIFI_DEFAULTS[d].pass, kBuiltIn);
  saveAll(list, n);
  prefs.begin("wifi", false);
  prefs.putUInt("ver", WIFI_SECRETS_VERSION);
  prefs.putChar("last", -1);      // indexes changed
  prefs.end();
  Serial.printf("[wifi] NVS updated from wifi_secrets.h (%u networks)\n", n);
}

bool tryConnect(const String& s, const String& p) {
  Serial.printf("[wifi] trying \"%s\"...\n", s.c_str());
  WiFi.begin(s.c_str(), p.c_str());
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < timeoutMs) delay(100);
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect();
  return false;
}

// Scan, then try visible known networks: last-good first, then by signal strength.
bool connectKnown() {
  Cred list[kMax];
  uint8_t n = loadAll(list);
  if (!n) { Serial.println("[wifi] no network stored"); return false; }
  int32_t best[kMax];
  for (uint8_t i = 0; i < kMax; i++) best[i] = INT32_MIN;
  int found = WiFi.scanNetworks();
  for (int j = 0; j < found; j++) {
    int i = findSsid(list, n, WiFi.SSID(j));
    if (i >= 0 && WiFi.RSSI(j) > best[i]) best[i] = WiFi.RSSI(j);
  }
  WiFi.scanDelete();

  int8_t last = -1;
  if (prefs.begin("wifi", true)) { last = prefs.getChar("last", -1); prefs.end(); }

  uint8_t order[kMax], m = 0;
  for (uint8_t i = 0; i < n; i++) if (best[i] != INT32_MIN) order[m++] = i;
  for (uint8_t a = 1; a < m; a++)                         // sort by RSSI, strongest first
    for (uint8_t b = a; b > 0 && best[order[b]] > best[order[b - 1]]; b--) std::swap(order[b], order[b - 1]);
  for (uint8_t a = 0; a < m; a++)                         // last-good network goes first
    if (order[a] == last) { for (uint8_t b = a; b > 0; b--) std::swap(order[b], order[b - 1]); break; }

  if (!m) Serial.println("[wifi] no known network in range");
  for (uint8_t a = 0; a < m; a++) {
    uint8_t i = order[a];
    if (tryConnect(list[i].ssid, list[i].pass)) {
      if (i != last) { prefs.begin("wifi", false); prefs.putChar("last", i); prefs.end(); }
      Serial.printf("[wifi] connected to \"%s\", IP %s\n", list[i].ssid.c_str(), WiFi.localIP().toString().c_str());
      return true;
    }
  }
  return false;
}

String htmlEscape(const String& s) {
  String o;
  for (char ch : s) {
    switch (ch) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += ch;
    }
  }
  return o;
}

String jsonEscape(const String& s) {
  String o;
  for (char ch : s) {
    if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
    else if ((uint8_t)ch < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", (uint8_t)ch); o += b; }
    else o += ch;
  }
  return o;
}

// Answer to "#wifi-list": the PC script (pull-wifi-from-board.ps1) reads these lines.
void dumpNetworks() {
  Cred list[kMax];
  uint8_t n = loadAll(list);
  Serial.printf("#wifi-begin {\"count\":%u,\"version\":\"0x%08lX\"}\n", n, (unsigned long)WIFI_SECRETS_VERSION);
  for (uint8_t i = 0; i < n; i++) {
    Serial.printf("#wifi {\"ssid\":\"%s\",\"pass\":\"%s\",\"origin\":\"%s\"}\n", jsonEscape(list[i].ssid).c_str(),
                  jsonEscape(list[i].pass).c_str(), list[i].origin == kPortal ? "portal" : "builtin");
  }
  Serial.println("#wifi-end");
}

void handleRoot() {
  int found = WiFi.scanNetworks();
  String opts;
  for (int j = 0; j < found; j++) opts += "<option value=\"" + htmlEscape(WiFi.SSID(j)) + "\">";
  WiFi.scanDelete();
  String page =
      "<!doctype html><meta name=viewport content='width=device-width'><title>ESP32 WiFi</title>"
      "<style>body{font-family:sans-serif;max-width:360px;margin:24px auto;padding:0 16px}"
      "input,button{width:100%;padding:10px;margin:6px 0;box-sizing:border-box}</style>"
      "<h3>WiFi setup</h3><form method=post action=/save>"
      "<input name=ssid list=n placeholder=SSID required><datalist id=n>" + opts + "</datalist>"
      "<input name=pass type=password placeholder=Password>"
      "<button>Save and restart</button></form>";
  server->send(200, "text/html", page);
}

void handleSave() {
  String s = server->arg("ssid"), p = server->arg("pass");
  if (s.isEmpty() || s.length() > 32 || p.length() > 63) { server->send(400, "text/plain", "Invalid SSID or password"); return; }
  wifinvs::addNetwork(s.c_str(), p.c_str());
  server->send(200, "text/plain", "Saved. Restarting...");
  delay(1000);
  ESP.restart();
}

void startPortal() {
  if (portalActive) return;
  // Switching mode while the PHY is still starting crashed the ESP32-S3 (core 3.2.0,
  // "Cache disabled but cached memory region accessed"): give the driver time to settle.
  delay(200);
  WiFi.mode(WIFI_AP_STA);
  delay(100);
  char name[24];
  snprintf(name, sizeof name, "ESP32-Setup-%04X", (uint16_t)(ESP.getEfuseMac() >> 32));
  WiFi.softAP(name, WIFI_PORTAL_PASSWORD);
  server = new WebServer(80);
  server->on("/", HTTP_GET, handleRoot);
  server->on("/save", HTTP_POST, handleSave);
  server->begin();
  portalActive = true;
  portalLastTry = millis();
  Serial.printf("[wifi] setup portal: join \"%s\" and open http://%s\n", name, WiFi.softAPIP().toString().c_str());
}

void stopPortal() {
  if (!portalActive) return;
  server->stop();
  delete server;
  server = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalActive = false;
}

void pollSerial() {
#if WIFI_NVS_SERIAL_CMDS
  if (Serial.available() && Serial.peek() == '#') {   // only lines starting with '#' are taken
    String line = Serial.readStringUntil('\n');
    wifinvs::handleSerialLine(line);
  }
#endif
}
}  // namespace

namespace wifinvs {
bool begin(uint32_t connectTimeoutMs) {
  timeoutMs = connectTimeoutMs;
  syncDefaults();               // NVS first, while the WiFi driver is still off
  WiFi.persistent(false);       // stop the WiFi driver from writing its own copy to flash on every begin()
  WiFi.setAutoReconnect(true);
  Cred probe[kMax];
  if (!loadAll(probe)) {        // nothing stored: go straight to the portal, no STA->AP_STA switch
    Serial.println("[wifi] no network stored");
    startPortal();
    return false;
  }
  WiFi.mode(WIFI_STA);
  if (connectKnown()) return true;
  startPortal();
  return false;
}

void loop() {
  pollSerial();
  if (portalActive) {
    server->handleClient();
    // Router may have been off at boot: retry when nobody is using the portal.
    if (millis() - portalLastTry > kPortalRetryMs && WiFi.softAPgetStationNum() == 0) {
      portalLastTry = millis();
      if (connectKnown()) stopPortal();
    }
    return;
  }
  if (WiFi.status() == WL_CONNECTED) { lostSince = 0; return; }
  if (!lostSince) { lostSince = millis(); return; }
  if (millis() - lostSince > kLostRetryMs) {   // auto-reconnect failed: maybe moved to another network
    lostSince = 0;
    if (!connectKnown()) startPortal();
  }
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }
bool isPortalActive() { return portalActive; }

bool addNetwork(const char* ssid, const char* pass) {
  Cred list[kMax];
  uint8_t n = loadAll(list);
  int i = upsert(list, n, ssid, pass, kPortal);
  saveAll(list, n);
  prefs.begin("wifi", false);
  prefs.putChar("last", (int8_t)i);
  prefs.end();
  return true;
}

void clearAll() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
}

bool handleSerialLine(const String& raw) {
  String line = raw;
  line.trim();
  if (line == "#wifi-list") { dumpNetworks(); return true; }
  return false;
}
}  // namespace wifinvs
