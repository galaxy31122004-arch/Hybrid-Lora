"""Generate the ThingsBoard dashboard for a firmware version.

Usage: python scripts/make_dashboard.py firmware/v0.5.4/dashboard/hybrid-lora-dashboard.json

Two dashboard states (tabs):
  - "Vận hành" (default): KPI banner, node list, light on/off buttons.
  - "Test nhanh": KPI banner, rssi/snr/testMode per node, Ping / Test mode /
    light buttons, RSSI and SNR history charts.
Buttons call server-side RPC on the row's device (setLight, ping, setTestMode),
which the gateway relays over LoRa. Built for ThingsBoard 4.x (Cloud).
"""
import json
import sys
import uuid

ALIAS_ID = str(uuid.uuid5(uuid.NAMESPACE_URL, "hybrid-lora/alias/nodes"))
GATEWAY_ALIAS_ID = str(uuid.uuid5(uuid.NAMESPACE_URL, "hybrid-lora/alias/gateway"))
# The ThingsBoard gateway device must have this name (see firmware README).
GATEWAY_NAME = "Hybrid-LoRa-Gateway"

# Minimal monochrome theme: near-black surfaces, white/grey text, hairline
# gradient borders. State is shown by border style and brightness, not hue.
BG = "#050505"
CARD = "#0c0c0c"
TEXT = "#ededed"
MUTED = "#6b6b6b"
NODE_COLORS = ["#ffffff", "#bdbdbd", "#8a8a8a", "#5c5c5c", "#d9d9d9", "#9e9e9e"]
# 1px border that is bright at the top-left corner and fades out.
GRADIENT_BORDER = ("linear-gradient(" + CARD + "," + CARD + ") padding-box,"
                   " linear-gradient(135deg, rgba(255,255,255,.55) 0%,"
                   " rgba(255,255,255,.06) 35%, rgba(255,255,255,.06) 70%,"
                   " rgba(255,255,255,.30) 100%) border-box")


def wid(name):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, f"hybrid-lora/widget/{name}"))


def key(name, label, decimals=None, units=None, cell=None, style=None, color="#d4d4d4"):  # neutral grey: no hue outside chart lines
    settings = {}
    if cell:  # JS body: value -> display text/HTML
        settings.update(useCellContentFunction=True, cellContentFunction=cell)
    if style:  # JS body: value -> CSS object
        settings.update(useCellStyleFunction=True, cellStyleFunction=style)
    k = {"name": name, "type": "timeseries", "label": label, "color": color,
         "settings": settings,
         "_hash": uuid.uuid5(uuid.NAMESPACE_URL, name + label).int % 1000 / 1000}
    if decimals is not None:
        k["decimals"] = decimals
    if units:
        k["units"] = units
    return k


# Cell formatters (value may arrive as string or number). Monochrome: "on/good"
# = bright text in a solid hairline frame, "weak" = dashed frame, "off" = dim.
FRAME = "border:'1px solid rgba(255,255,255,.55)', borderRadius:'999px', letterSpacing:'.04em'"
DASHED = "border:'1px dashed rgba(255,255,255,.45)', borderRadius:'999px', letterSpacing:'.04em'"
DIM = "{color:'" + MUTED + "'}"

ONLINE = "return (value === true || value === 'true') ? '● ONLINE' : '○ OFFLINE';"
ONLINE_STYLE = ("var on = value === true || value === 'true';"
                "return on ? {color:'#ffffff', fontWeight:600, letterSpacing:'.08em'}"
                " : {color:'" + MUTED + "', letterSpacing:'.08em'};")
LIGHT = "return Number(value) === 1 ? 'BẬT' : 'tắt';"
LIGHT_STYLE = ("return Number(value) === 1 ? {color:'#ffffff', fontWeight:600, textAlign:'center', "
               + FRAME + "} : " + DIM + ";")
MODE = "return Number(value) === 1 ? 'remote' : 'local';"
MODE_STYLE = ("return {color: Number(value) === 1 ? '#ffffff' : '#a3a3a3',"
              " fontFamily:'monospace', letterSpacing:'.06em'};")
TEST = "return Number(value) === 1 ? 'TEST' : '·';"
TEST_STYLE = ("return Number(value) === 1 ? {color:'#ffffff', fontWeight:600, textAlign:'center', "
              + DASHED + "} : " + DIM + ";")
LUX = "return Number(value) === 65535 ? 'n/a' : value + ' lx';"
LUX_STYLE = ("return Number(value) === 65535 ? {color:'" + MUTED + "', fontStyle:'italic'}"
             " : {color:'#ffffff', fontFamily:'monospace'};")
RSSI = "return Math.round(Number(value)) + ' dBm';"
# Brightness encodes strength: strong = white, medium = grey, weak = dim.
RSSI_STYLE = ("var r = Number(value);"
              "var c = r >= -110 ? '#ffffff' : (r >= -125 ? '#a3a3a3' : '" + MUTED + "');"
              "return {color:c, fontFamily:'monospace', fontWeight:600};")
# Link quality at SF12: decoding fails near SNR -20 dB. Text bar shows margin.
SNR = ("var s = Number(value); var m = Math.max(0, s + 20);"
       "var bars = Math.min(10, Math.round(m / 3));"
       "return '▮'.repeat(bars) + '▯'.repeat(10 - bars) + '  ' + s.toFixed(1)"
       " + ' dB · dư ' + (s + 20).toFixed(1);")
SNR_STYLE = ("var m = Number(value) + 20;"
             "var st = m >= 10 ? {" + FRAME + "} : (m >= 5 ? {" + DASHED + "} : {border:'1px dotted rgba(255,255,255,.35)', borderRadius:'999px'});"
             "st.color = m >= 5 ? '#ffffff' : '#a3a3a3'; st.fontFamily = 'monospace';"
             "st.whiteSpace = 'nowrap'; return st;")

# Test STATUS packet delivery over the last 20 packets (v0.5.2).
PDR = "return (value === '' || value === null || value === undefined) ? '—' : Math.round(Number(value)) + ' %';"
PDR_STYLE = ("var p = Number(value);"
             "var c = p >= 90 ? '#ffffff' : (p >= 70 ? '#a3a3a3' : '" + MUTED + "');"
             "return {color:c, fontFamily:'monospace', fontWeight:600};")

# Node's own Wi-Fi report (v0.5.4): nodeSeen = epoch seconds of the last
# report (every 10 s). Older than 30 s = node off or out of Wi-Fi.
NODE_WIFI = ("var t = Number(value); if (!t) return '—';"
             "var age = Math.round(Date.now() / 1000 - t);"
             "if (age < 30) return '● BẬT';"
             "return '○ ' + (age < 120 ? age + ' s' : Math.round(age / 60) + ' phút') + ' trước';")
NODE_WIFI_STYLE = ("var t = Number(value); var on = t && (Date.now() / 1000 - t) < 30;"
                   "return on ? {color:'#ffffff', fontWeight:600, letterSpacing:'.08em', whiteSpace:'nowrap'}"
                   " : {color:'" + MUTED + "', letterSpacing:'.08em', whiteSpace:'nowrap'};")


def rpc_action(label, icon, method, params, ok_text):
    body = json.dumps({"method": method, "params": params, "timeout": 60000})
    code = (
        "const req = " + body + ";\n"
        "widgetContext.showSuccessToast('Đang gửi " + label + " tới ' + entityName + ' (SF12, có thể mất vài giây)...', 3000);\n"
        "widgetContext.deviceService.sendTwoWayRpcCommand(entityId.id, req).subscribe(\n"
        "  (resp) => {\n"
        "    if (resp && resp.success === false) {\n"
        "      widgetContext.showErrorToast(entityName + ': lỗi ' + (resp.error || 'không rõ'), 'top', 'center', null, true);\n"
        "    } else {\n"
        "      widgetContext.showSuccessToast(entityName + ': " + ok_text + "', 3000);\n"
        "    }\n"
        "  },\n"
        "  (err) => widgetContext.showErrorToast(entityName + ': không phản hồi (timeout hoặc gateway offline)', 'top', 'center', null, true)\n"
        ");\n"
    )
    return {"name": label, "icon": icon, "useShowWidgetActionFunction": None,
            "showWidgetActionFunction": "return true;", "type": "custom",
            "customFunction": code, "openInSeparateDialog": False,
            "openInPopover": False,
            "id": str(uuid.uuid5(uuid.NAMESPACE_URL, f"action/{method}/{label}"))}


def nav_action(label, icon, state_id):
    return {"name": label, "icon": icon, "type": "openDashboardState",
            "targetDashboardStateId": state_id, "setEntityId": False,
            "stateEntityParamName": None, "openInSeparateDialog": False,
            "openInPopover": False,
            "id": str(uuid.uuid5(uuid.NAMESPACE_URL, f"nav/{state_id}"))}


def card_style(widget_config):
    widget_config.update({
        "backgroundColor": CARD, "color": TEXT, "padding": "10px",
        "dropShadow": False, "enableFullscreen": True,
        "widgetStyle": {"borderRadius": "16px", "border": "1px solid transparent",
                        "background": GRADIENT_BORDER,
                        "boxShadow": "0 0 0 1px rgba(255,255,255,.02), 0 18px 40px -24px rgba(255,255,255,.18)"},
        "titleStyle": {"fontSize": "13px", "fontWeight": 500, "color": "#ffffff",
                       "letterSpacing": ".18em", "textTransform": "uppercase"},
    })
    return widget_config


BANNER_FN = r"""
// ThingsBoard 4.x passes one formatted row per entity ({entityName, <key>: value});
// older versions pass raw datasource data. Accept both.
var nodes = {};
(data || []).forEach(function (d) {
  if (d.entityName !== undefined && d.dataKey === undefined) {
    var row = nodes[d.entityName] = nodes[d.entityName] || {};
    ['online', 'lightState', 'testMode', 'snr'].forEach(function (k) {
      if (d[k] !== undefined && d[k] !== '') row[k] = d[k];
    });
    return;
  }
  var n = d.datasource && d.datasource.entityName;
  if (!n) return;
  var v = d.data && d.data.length ? d.data[d.data.length - 1][1] : null;
  (nodes[n] = nodes[n] || {})[d.dataKey.name] = v;
});
var names = Object.keys(nodes), online = 0, lights = 0, tests = 0, worst = null;
names.forEach(function (n) {
  var x = nodes[n];
  if (x.online === true || x.online === 'true') online++;
  if (Number(x.lightState) === 1) lights++;
  if (Number(x.testMode) === 1) tests++;
  if (x.snr !== null && x.snr !== undefined && x.snr !== '' &&
      (worst === null || Number(x.snr) < worst.snr)) worst = {name: n, snr: Number(x.snr)};
});
function tile(cls, label, value, sub) {
  return '<div class="tile ' + cls + '"><div class="v">' + value + '</div><div class="l">' +
         label + '</div>' + (sub ? '<div class="s">' + sub + '</div>' : '') + '</div>';
}
var worstText = worst ? (worst.snr + 20).toFixed(1) + ' dB' : '—';
return '<div class="banner"><div class="head"><div class="t"><b>Hybrid</b>·LoRa</div>' +
  '<div class="st">__SUBTITLE__</div>' +
  '<div class="tabs">__TABS__</div></div><div class="tiles">' +
  tile('', 'Node', names.length) +
  tile('hl', 'Online', online, names.length ? Math.round(online * 100 / names.length) + '%' : '') +
  tile('', 'Đèn bật', lights) +
  tile('', 'Đang test', tests) +
  tile(worst && worst.snr + 20 < 5 ? 'hl' : '', 'Dư tín hiệu min', worstText, worst ? worst.name : '') +
  '</div></div>';
"""

BANNER_CSS = """
.banner { position: relative; height: 100%; box-sizing: border-box; padding: 16px 22px;
  border-radius: 18px; color: #ededed; display: flex; align-items: center; gap: 28px;
  flex-wrap: wrap; border: 1px solid transparent;
  background:
    radial-gradient(120% 140% at 0% 0%, rgba(255,255,255,.07), transparent 55%) padding-box,
    linear-gradient(#0a0a0a, #0a0a0a) padding-box,
    linear-gradient(120deg, rgba(255,255,255,.7), rgba(255,255,255,.05) 30%,
                    rgba(255,255,255,.05) 70%, rgba(255,255,255,.45)) border-box;
  box-shadow: 0 24px 60px -30px rgba(255,255,255,.25); }
.banner::after { content: ''; position: absolute; left: 22px; right: 22px; bottom: 0; height: 1px;
  background: linear-gradient(90deg, transparent, rgba(255,255,255,.6), transparent); }
.banner .head .t { font-size: 24px; font-weight: 300; letter-spacing: .32em; text-transform: uppercase; }
.banner .head .t b { font-weight: 700; }
.banner .head .st { margin-top: 4px; font-size: 11px; letter-spacing: .2em; text-transform: uppercase; color: #8a8a8a; }
.banner .tiles { display: flex; gap: 14px; flex-wrap: wrap; flex: 1; justify-content: flex-end; }
.tile { position: relative; min-width: 108px; padding: 10px 16px 8px; border-radius: 10px;
  background: linear-gradient(180deg, rgba(255,255,255,.04), rgba(255,255,255,0));
  border: 1px solid rgba(255,255,255,.10); }
/* viewfinder corners */
.tile::before, .tile::after { content: ''; position: absolute; width: 10px; height: 10px; }
.tile::before { top: -1px; left: -1px; border-top: 1px solid #fff; border-left: 1px solid #fff; border-top-left-radius: 10px; }
.tile::after { bottom: -1px; right: -1px; border-bottom: 1px solid #fff; border-right: 1px solid #fff; border-bottom-right-radius: 10px; }
.tile .v { font-size: 28px; font-weight: 200; line-height: 1.05; font-variant-numeric: tabular-nums; color: #fff; }
.tile .l { margin-top: 2px; font-size: 10px; letter-spacing: .16em; text-transform: uppercase; color: #9a9a9a; }
.tile .s { font-size: 10px; color: #6b6b6b; letter-spacing: .08em; }
.tile.hl { border-color: rgba(255,255,255,.35); }
/* Tab switch inside the banner (element-click actions open the other state) */
.banner .tabs { display: flex; gap: 8px; margin-top: 12px; }
.banner .tab { cursor: pointer; padding: 6px 14px; border-radius: 999px; font-size: 11px;
  letter-spacing: .16em; text-transform: uppercase; color: #8a8a8a;
  border: 1px solid rgba(255,255,255,.12); user-select: none; }
.banner .tab:hover { color: #fff; border-color: rgba(255,255,255,.4); }
.banner .tab.on { color: #0a0a0a; background: #ededed; border-color: #ededed; cursor: default; }
"""


STATE_TABS = [("default", "Vận hành"), ("test", "Test nhanh")]


def banner(name, subtitle, state):
    keys = [key(k, k) for k in ("online", "lightState", "testMode", "snr")]
    # Tabs are plain elements; an "elementClick" action whose name equals the
    # element id switches the dashboard state. Works on phones too, where the
    # table header buttons are easy to miss.
    tabs = "".join(
        f'<span id="tab-{sid}" class="tab{" on" if sid == state else ""}">{label}</span>'
        for sid, label in STATE_TABS)
    clicks = [{"name": f"tab-{sid}", "icon": "tab", "type": "openDashboardState",
               "targetDashboardStateId": sid, "setEntityId": False,
               "stateEntityParamName": None, "openInSeparateDialog": False,
               "openInPopover": False,
               "id": str(uuid.uuid5(uuid.NAMESPACE_URL, f"tab/{name}/{sid}"))}
              for sid, _ in STATE_TABS if sid != state]
    return {
        "typeFullFqn": "system.cards.markdown_card",
        "type": "latest",
        "sizeX": 24, "sizeY": 3,
        "config": {
            "showTitle": False, "backgroundColor": "transparent", "padding": "0px",
            "dropShadow": False, "enableFullscreen": False,
            "settings": {"useMarkdownTextFunction": True,
                         "markdownTextFunction": BANNER_FN.replace("__SUBTITLE__", subtitle)
                                                         .replace("__TABS__", tabs.replace("'", "\\'")),
                         "markdownCss": BANNER_CSS, "applyDefaultMarkdownStyle": False},
            "datasources": [{"type": "entity", "entityAliasId": ALIAS_ID, "dataKeys": keys}],
            "actions": {"elementClick": clicks},
        },
        "id": wid(name),
    }


TABLE_CSS = """
.mat-mdc-row, .mat-mdc-row .mat-mdc-cell,
.mat-mdc-row .mat-mdc-table-sticky { background: #0a0a0a !important; transition: background .15s; }
.mat-mdc-row:hover, .mat-mdc-row:hover .mat-mdc-cell,
.mat-mdc-row:hover .mat-mdc-table-sticky { background: #141414 !important; }
.mat-mdc-row:hover .mat-mdc-cell:first-child { box-shadow: inset 2px 0 0 rgba(255,255,255,.55); }
"""


def table(name, title, keys, cell_actions, header_actions):
    config = card_style({
        "timewindow": {"realtime": {"timewindowMs": 86400000}},
        "showTitle": True, "title": title,
        # ThingsBoard 4.x shows settings.entitiesTitle as the table title.
        "settings": {"entitiesTitle": title,
                     "enableSearch": True, "enableSelectColumnDisplay": True,
                     "enableStickyHeader": True, "enableStickyAction": True,
                     "displayEntityName": True, "entityNameColumnTitle": "Node",
                     "displayEntityLabel": False, "displayEntityType": False,
                     "displayPagination": True, "defaultPageSize": 50,
                     "defaultSortOrder": "entityName"},
        "datasources": [{"type": "entity", "entityAliasId": ALIAS_ID, "dataKeys": keys}],
        "actions": {"actionCellButton": cell_actions, "headerButton": header_actions},
        "showLegend": False,
        # Hover: ThingsBoard paints the whole row and the sticky action column
        # light grey. Keep a faint row tint and make the action column follow it.
        "widgetCss": TABLE_CSS,
    })
    return {"typeFullFqn": "system.cards.entities_table", "type": "latest",
            "sizeX": 24, "sizeY": 9, "config": config, "id": wid(name)}


def ts_table(name, title, keys, alias, header_actions=(), hours=6):
    """One row per received value with its time (like the gateway's Serial
    log). ThingsBoard shows one tab per device and offers CSV export."""
    config = card_style({
        "timewindow": {"realtime": {"timewindowMs": hours * 3600000}},
        "showTitle": True, "title": title,
        "settings": {"enableSearch": True, "enableStickyHeader": True,
                     "showTimestamp": True, "displayPagination": True,
                     "defaultPageSize": 20, "hideEmptyLines": True,
                     "enableSelectColumnDisplay": False},
        "datasources": [{"type": "entity", "entityAliasId": alias, "dataKeys": keys}],
        "actions": {"headerButton": list(header_actions)},
        "showLegend": False,
        "widgetCss": TABLE_CSS,
    })
    return {"typeFullFqn": "system.cards.timeseries_table", "type": "timeseries",
            "sizeX": 24, "sizeY": 8, "config": config, "id": wid(name)}


# Field points: the phone's browser GPS, stored on the gateway device.
# "Đặt vị trí Gateway" saves the gateway's position (server attributes
# latitude/longitude); "Ghi điểm đo" saves the node's spot as telemetry with
# the distance to the gateway, so each spot lines up in time with the packets
# in "Nhật ký test". Needs location permission in the browser (HTTPS).
GEO_JS = """
const http = widgetContext.http;
const err = (m) => widgetContext.showErrorToast(m, 'top', 'center', null, true);
function withGateway(cb) {
  http.get('/api/tenant/devices?deviceName=__GW__').subscribe(
    (d) => cb(d.id.id),
    () => err('Chưa có thiết bị __GW__ trên ThingsBoard'));
}
function withPosition(cb) {
  if (!navigator.geolocation) { err('Trình duyệt không hỗ trợ GPS'); return; }
  widgetContext.showSuccessToast('Đang lấy vị trí GPS...', 2000);
  navigator.geolocation.getCurrentPosition(
    (p) => cb(p.coords),
    (e) => err('Không lấy được vị trí: ' + e.message + ' (cho phép quyền vị trí cho trang này)'),
    {enableHighAccuracy: true, timeout: 20000, maximumAge: 0});
}
function distanceM(a, b, c, d) {
  const r = Math.PI / 180, R = 6371000;
  const x = Math.sin((c - a) * r / 2) ** 2 +
            Math.cos(a * r) * Math.cos(c * r) * Math.sin((d - b) * r / 2) ** 2;
  return Math.round(2 * R * Math.asin(Math.sqrt(x)));
}
"""

MARK_POINT_JS = GEO_JS + """
const note = window.prompt('Ghi chú cho điểm đo (vd. cột đèn 12), có thể bỏ trống:', '');
if (note === null) return;
withGateway((id) => withPosition((c) => {
  http.get('/api/plugins/telemetry/DEVICE/' + id +
           '/values/attributes/SERVER_SCOPE?keys=latitude,longitude').subscribe((attrs) => {
    const a = {};
    (attrs || []).forEach((x) => a[x.key] = Number(x.value));
    const point = {pointLat: c.latitude, pointLon: c.longitude,
                   pointAcc: Math.round(c.accuracy), pointLabel: note || '—'};
    if (a.latitude !== undefined && a.longitude !== undefined) {
      point.pointDist = distanceM(a.latitude, a.longitude, c.latitude, c.longitude);
    }
    http.post('/api/plugins/telemetry/DEVICE/' + id + '/timeseries/ANY', point).subscribe(
      () => widgetContext.showSuccessToast('Đã ghi điểm đo' +
            (point.pointDist !== undefined ? ' · cách Gateway ' + point.pointDist + ' m' :
             ' (chưa đặt vị trí Gateway nên chưa có khoảng cách)') +
            ' · sai số ±' + point.pointAcc + ' m', 4000),
      () => err('Không lưu được điểm đo'));
  });
}));
"""

SET_GATEWAY_JS = GEO_JS + """
withGateway((id) => withPosition((c) => {
  http.post('/api/plugins/telemetry/DEVICE/' + id + '/attributes/SERVER_SCOPE',
            {latitude: c.latitude, longitude: c.longitude}).subscribe(
    () => widgetContext.showSuccessToast('Đã lưu vị trí Gateway · sai số ±' +
                                         Math.round(c.accuracy) + ' m', 4000),
    () => err('Không lưu được vị trí Gateway'));
}));
"""


def ts_key(name, ktype="timeseries"):
    return {"name": name, "type": ktype, "label": name, "settings": {}, "color": "#ffffff"}


def field_map(name, title, hours=24):
    """ThingsBoard 4.x map (system "route_map"): the walk through the field
    points as a dark path with a dot per point, plus the gateway marker.
    Only the fields set here are given; the widget fills the rest with its
    defaults (OpenStreetMap tiles, controls)."""
    # Dark marks: map tiles are light, so white points would vanish.
    white = {"type": "constant", "color": "#111111"}
    tip = ("<b>${pointLabel}</b><br/>cách Gateway ${pointDist} m<br/>"
           "sai số ±${pointAcc} m<br/>${ts}")
    settings = {
        "mapType": "geoMap",
        "fitMapBounds": True,
        "trips": [{
            "dsType": "entity", "dsEntityAliasId": GATEWAY_ALIAS_ID, "dsLabel": "Điểm đo",
            "xKey": ts_key("pointLat"), "yKey": ts_key("pointLon"),
            "additionalDataKeys": [ts_key("pointLabel"), ts_key("pointDist"), ts_key("pointAcc")],
            "label": {"show": False, "type": "pattern", "pattern": ""},
            "tooltip": {"show": True, "trigger": "click", "autoclose": True,
                        "type": "pattern", "pattern": tip, "offsetX": 0, "offsetY": -1},
            "showPath": True, "pathStrokeWeight": 2, "pathStrokeColor": white,
            "showPoints": True, "pointSize": 12, "pointColor": white,
            "pointTooltip": {"show": True, "trigger": "click", "autoclose": True,
                             "type": "pattern", "pattern": tip, "offsetX": 0, "offsetY": -1},
        }],
        "markers": [{
            "dsType": "entity", "dsEntityAliasId": GATEWAY_ALIAS_ID, "dsLabel": "Gateway",
            "xKey": ts_key("latitude", "attribute"), "yKey": ts_key("longitude", "attribute"),
            "additionalDataKeys": [],
            "label": {"show": True, "type": "pattern", "pattern": "Gateway"},
            "tooltip": {"show": False, "trigger": "click", "autoclose": True,
                        "type": "pattern", "pattern": "", "offsetX": 0, "offsetY": -1},
        }],
        "tripTimeline": {"showTimelineControl": False},
    }
    config = card_style({
        "useDashboardTimewindow": False, "displayTimewindow": True,
        "timewindow": {"realtime": {"realtimeType": 0, "timewindowMs": hours * 3600000},
                       "aggregation": {"type": "NONE", "limit": 50000}},
        "showTitle": True, "title": title, "settings": settings, "datasources": [],
    })
    return {"typeFullFqn": "system.route_map", "type": "timeseries",
            "sizeX": 24, "sizeY": 10, "config": config, "id": wid(name)}


def js_action(label, icon, code):
    return {"name": label, "icon": icon, "type": "custom", "customFunction": code,
            "useShowWidgetActionFunction": None, "showWidgetActionFunction": "return true;",
            "openInSeparateDialog": False, "openInPopover": False,
            "id": str(uuid.uuid5(uuid.NAMESPACE_URL, f"js/{label}"))}


def chart(name, title, data_key, alias=None, width=12):
    config = card_style({
        "timewindow": {"realtime": {"timewindowMs": 3600000}},
        "showTitle": True, "title": title, "showLegend": True,
        # smoothLines: curved interpolation between samples.
        "settings": {"shadowSize": 0, "fontColor": "#8a8a8a", "fontSize": 11,
                     "smoothLines": True, "showDataPoints": False,
                     "grid": {"color": "#1c1c1c", "tickColor": "#1c1c1c", "outlineWidth": 0}},
        "datasources": [{"type": "entity", "entityAliasId": alias or ALIAS_ID,
                         "dataKeys": [data_key]}],
    })
    return {"typeFullFqn": "system.charts.basic_timeseries", "type": "timeseries",
            "sizeX": width, "sizeY": 8, "config": config, "id": wid(name)}


# Height in grid rows on a phone (narrow screens stack widgets in one column).
MOBILE_HEIGHT = {"system.cards.markdown_card": 5, "system.cards.entities_table": 7,
                 "system.cards.timeseries_table": 8, "system.charts.basic_timeseries": 6,
                 "system.route_map": 9}


def layout(widgets):
    """Stack widgets top to bottom; widgets narrower than 24 share a row."""
    cells, row, col, row_height = {}, 0, 0, 0
    for w in widgets:
        if col + w["sizeX"] > 24:
            row, col, row_height = row + row_height, 0, 0
        # Phone: one column in list order, with a fixed height per widget.
        cells[w["id"]] = {"sizeX": w["sizeX"], "sizeY": w["sizeY"], "row": row, "col": col,
                          "mobileOrder": len(cells),
                          "mobileHeight": MOBILE_HEIGHT.get(w["typeFullFqn"], w["sizeY"])}
        col += w["sizeX"]
        row_height = max(row_height, w["sizeY"])
    return {"main": {"widgets": cells,
                     "gridSettings": {"backgroundColor": BG, "color": TEXT, "columns": 24,
                                      "margin": 12, "outerMargin": True,
                                      "backgroundSizeMode": "100%"}}}


def build():
    light_on = rpc_action("Bật đèn", "lightbulb", "setLight", True, "đã bật đèn")
    light_off = rpc_action("Tắt đèn", "lightbulb_outline", "setLight", False, "đã tắt đèn")
    ping = rpc_action("Ping", "network_ping", "ping", {}, "đã trả lời, RSSI/SNR sẽ cập nhật sau STATUS")
    test_on = rpc_action("Test 30 phút", "science", "setTestMode", 30, "đã bật test 30 phút (STATUS mỗi 30 s)")
    test_off = rpc_action("Tắt test", "stop_circle", "setTestMode", 0, "đã tắt test")

    banner_op = banner("banner-operation", "Vận hành · LoRa SF12 · 433 MHz", "default")
    banner_test = banner("banner-test", "Test nhanh · tín hiệu · ping · test mode", "test")
    operation = table(
        "operation", "Danh sách Node",
        [key("online", "Trạng thái", cell=ONLINE, style=ONLINE_STYLE),
         key("lightState", "Đèn", cell=LIGHT, style=LIGHT_STYLE),
         key("mode", "Chế độ", cell=MODE, style=MODE_STYLE),
         key("lux", "Ánh sáng", cell=LUX, style=LUX_STYLE),
         key("healthFlags", "Health")],
        [light_on, light_off],
        [nav_action("Sang Test nhanh", "science", "test")])
    test_table = table(
        "test", "Tín hiệu · SF12 mất sóng tại SNR −20 dB",
        [key("online", "Trạng thái", cell=ONLINE, style=ONLINE_STYLE),
         key("nodeSeen", "Node WiFi", cell=NODE_WIFI, style=NODE_WIFI_STYLE),
         key("rssi", "RSSI", cell=RSSI, style=RSSI_STYLE),
         key("snr", "SNR · dư tín hiệu", cell=SNR, style=SNR_STYLE),
         key("pdr", "Gói tới (20)", cell=PDR, style=PDR_STYLE),
         key("lost", "Mất", cell="return (value === '' || value === undefined) ? '—' : value;", style="return " + DIM + ";"),
         key("nodeTx", "Đã phát", cell="return (value === '' || value === undefined) ? '—' : value;", style="return " + DIM + ";"),
         key("testMode", "Test", cell=TEST, style=TEST_STYLE),
         key("lightState", "Đèn", cell=LIGHT, style=LIGHT_STYLE)],
        [ping, test_on, test_off, light_on, light_off],
        [nav_action("Về Vận hành", "dashboard", "default")])
    # ${entityName} puts the node name in each legend entry.
    rssi_chart = chart("rssi", "RSSI · dBm",
                       key("rssi", "${entityName}", 0, "dBm", color=NODE_COLORS[0]))
    snr_chart = chart("snr", "SNR · dB",
                      key("snr", "${entityName}", 1, "dB", color=NODE_COLORS[1]))

    noise_chart = chart("noise", "Nhiễu nền kênh uplink · dBm · ~10 s/mẫu",
                        key("noiseFloor", "Nhiễu nền", 0, "dBm", color="#a3a3a3"),
                        alias=GATEWAY_ALIAS_ID, width=24)

    log = ts_table("test-log", "Nhật ký test · mỗi gói một dòng · tải CSV ở góc phải",
                   [key("seq", "Seq"),
                    key("rssi", "RSSI", cell=RSSI, style=RSSI_STYLE),
                    key("snr", "SNR", 1, "dB"),
                    key("lost", "Mất"),
                    key("pdr", "Gói tới (20)", cell=PDR, style=PDR_STYLE)],
                   ALIAS_ID)
    points = ts_table("points", "Điểm đo · GPS điện thoại",
                      [key("pointLabel", "Ghi chú"),
                       key("pointDist", "Cách Gateway", 0, "m"),
                       key("pointAcc", "Sai số", 0, "m"),
                       key("pointLat", "Vĩ độ", 6), key("pointLon", "Kinh độ", 6)],
                      GATEWAY_ALIAS_ID,
                      [js_action("Ghi điểm đo", "add_location", MARK_POINT_JS.replace("__GW__", GATEWAY_NAME)),
                       js_action("Đặt vị trí Gateway", "my_location",
                                 SET_GATEWAY_JS.replace("__GW__", GATEWAY_NAME))],
                      hours=24)

    field = field_map("field-map", "Bản đồ điểm đo · bấm vào chấm để xem chi tiết")

    widgets = [banner_op, banner_test, operation, test_table, rssi_chart, snr_chart,
               noise_chart, log, points, field]
    return {
        "title": "Hybrid-LoRa",
        "name": "Hybrid-LoRa",
        "configuration": {
            "description": "Generated by scripts/make_dashboard.py",
            "widgets": {w["id"]: w for w in widgets},
            "states": {
                "default": {"name": "Vận hành", "root": True,
                            "layouts": layout([banner_op, operation])},
                "test": {"name": "Test nhanh", "root": False,
                         "layouts": layout([banner_test, test_table, points, field, log,
                                            rssi_chart, snr_chart, noise_chart])},
            },
            "entityAliases": {
                GATEWAY_ALIAS_ID: {"id": GATEWAY_ALIAS_ID, "alias": "Gateway",
                                   "filter": {"type": "entityName", "resolveMultiple": False,
                                              "entityType": "DEVICE",
                                              "entityNameFilter": GATEWAY_NAME}},
                ALIAS_ID: {"id": ALIAS_ID, "alias": "Tất cả Node",
                           "filter": {"type": "entityName", "resolveMultiple": True,
                                      "entityType": "DEVICE", "entityNameFilter": "Node-"}},
            },
            "filters": {},
            "timewindow": {"displayValue": "", "selectedTab": 0,
                           "realtime": {"realtimeType": 0, "interval": 1000,
                                        "timewindowMs": 3600000},
                           "aggregation": {"type": "NONE", "limit": 25000}},
            "settings": {"stateControllerId": "entity", "showTitle": False,
                         "showDashboardsSelect": False, "showEntitiesSelect": False,
                         "showDashboardTimewindow": True, "showDashboardExport": True,
                         "toolbarAlwaysOpen": True},
        },
    }


if __name__ == "__main__":
    with open(sys.argv[1], "w", encoding="utf-8", newline="\n") as f:
        json.dump(build(), f, ensure_ascii=False, indent=2)
        f.write("\n")
    print("wrote", sys.argv[1])
