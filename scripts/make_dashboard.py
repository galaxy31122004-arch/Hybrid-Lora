"""Generate the ThingsBoard dashboard for a firmware version.

Usage: python scripts/make_dashboard.py firmware/v0.5.0/dashboard/hybrid-lora-dashboard.json

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


def key(name, label, decimals=None, units=None, cell=None, style=None, color="#38bdf8"):
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
  '<div class="st">__SUBTITLE__</div></div><div class="tiles">' +
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
"""


def banner(name, subtitle):
    keys = [key(k, k) for k in ("online", "lightState", "testMode", "snr")]
    return {
        "typeFullFqn": "system.cards.markdown_card",
        "type": "latest",
        "sizeX": 24, "sizeY": 3,
        "config": {
            "showTitle": False, "backgroundColor": "transparent", "padding": "0px",
            "dropShadow": False, "enableFullscreen": False,
            "settings": {"useMarkdownTextFunction": True,
                         "markdownTextFunction": BANNER_FN.replace("__SUBTITLE__", subtitle),
                         "markdownCss": BANNER_CSS, "applyDefaultMarkdownStyle": False},
            "datasources": [{"type": "entity", "entityAliasId": ALIAS_ID, "dataKeys": keys}],
        },
        "id": wid(name),
    }


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
    })
    return {"typeFullFqn": "system.cards.entities_table", "type": "latest",
            "sizeX": 24, "sizeY": 9, "config": config, "id": wid(name)}


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


def layout(widgets):
    """Stack widgets top to bottom; widgets narrower than 24 share a row."""
    cells, row, col, row_height = {}, 0, 0, 0
    for w in widgets:
        if col + w["sizeX"] > 24:
            row, col, row_height = row + row_height, 0, 0
        cells[w["id"]] = {"sizeX": w["sizeX"], "sizeY": w["sizeY"], "row": row, "col": col}
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
    test_on = rpc_action("Test 30 phút", "science", "setTestMode", 30, "đã bật test 30 phút (STATUS mỗi 15 s)")
    test_off = rpc_action("Tắt test", "stop_circle", "setTestMode", 0, "đã tắt test")

    banner_op = banner("banner-operation", "Vận hành · LoRa SF12 · 433 MHz")
    banner_test = banner("banner-test", "Test nhanh · tín hiệu · ping · test mode")
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
         key("rssi", "RSSI", cell=RSSI, style=RSSI_STYLE),
         key("snr", "SNR · dư tín hiệu", cell=SNR, style=SNR_STYLE),
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

    widgets = [banner_op, banner_test, operation, test_table, rssi_chart, snr_chart,
               noise_chart]
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
                         "layouts": layout([banner_test, test_table, rssi_chart, snr_chart,
                                            noise_chart])},
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
