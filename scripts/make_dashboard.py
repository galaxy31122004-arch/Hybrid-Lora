"""Generate the ThingsBoard dashboard for a firmware version.

Usage: python scripts/make_dashboard.py firmware/v0.5.0/dashboard/hybrid-lora-dashboard.json

Two dashboard states (tabs):
  - "Vận hành" (default): node list, online/light/mode, light on/off buttons.
  - "Test nhanh": rssi/snr/testMode per node, Ping / Test mode / light buttons,
    and an RSSI/SNR history chart.
Buttons call server-side RPC on the row's device (setLight, ping, setTestMode),
which the gateway relays over LoRa.
"""
import json
import sys
import uuid

ALIAS_ID = str(uuid.uuid5(uuid.NAMESPACE_URL, "hybrid-lora/alias/nodes"))


def wid(name):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, f"hybrid-lora/widget/{name}"))


def key(name, label, decimals=None, units=None):
    k = {"name": name, "type": "timeseries", "label": label,
         "color": "#2196f3", "settings": {},
         "_hash": uuid.uuid5(uuid.NAMESPACE_URL, name).int % 1000 / 1000}  # stable across runs
    if decimals is not None:
        k["decimals"] = decimals
    if units:
        k["units"] = units
    return k


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
            "openInPopover": False, "id": str(uuid.uuid5(uuid.NAMESPACE_URL, f"action/{method}/{label}"))}


def nav_action(label, icon, state_id):
    return {"name": label, "icon": icon, "type": "openDashboardState",
            "targetDashboardStateId": state_id, "setEntityId": False,
            "stateEntityParamName": None, "openInSeparateDialog": False,
            "openInPopover": False, "id": str(uuid.uuid5(uuid.NAMESPACE_URL, f"nav/{state_id}"))}


def table(name, title, keys, cell_actions, header_actions):
    return {
        "typeFullFqn": "system.cards.entities_table",
        "type": "latest",
        "sizeX": 24, "sizeY": 10,
        "config": {
            "timewindow": {"realtime": {"timewindowMs": 86400000}},
            "showTitle": True, "title": title, "backgroundColor": "rgb(255, 255, 255)",
            "color": "rgba(0, 0, 0, 0.87)", "padding": "4px",
            "settings": {"enableSearch": True, "enableSelectColumnDisplay": True,
                         "enableStickyHeader": True, "enableStickyAction": True,
                         "displayEntityName": True, "entityNameColumnTitle": "Node",
                         "displayEntityLabel": False, "displayEntityType": False,
                         "displayPagination": True, "defaultPageSize": 50,
                         "defaultSortOrder": "entityName"},
            "datasources": [{"type": "entity", "entityAliasId": ALIAS_ID,
                             "dataKeys": keys}],
            "actions": {"actionCellButton": cell_actions, "headerButton": header_actions},
            "showLegend": False, "dropShadow": True, "enableFullscreen": True,
        },
        "id": wid(name),
    }


def chart(name, title):
    return {
        "typeFullFqn": "system.charts.basic_timeseries",
        "type": "timeseries",
        "sizeX": 24, "sizeY": 8,
        "config": {
            "timewindow": {"realtime": {"timewindowMs": 3600000}},
            "showTitle": True, "title": title, "showLegend": True,
            "settings": {"shadowSize": 4, "fontColor": "#545454", "fontSize": 10},
            "datasources": [{"type": "entity", "entityAliasId": ALIAS_ID,
                             "dataKeys": [key("rssi", "RSSI", 0, "dBm"),
                                          key("snr", "SNR", 1, "dB")]}],
            "dropShadow": True, "enableFullscreen": True,
        },
        "id": wid(name),
    }


def layout(widgets):
    cells, row = {}, 0
    for w in widgets:
        cells[w["id"]] = {"sizeX": w["sizeX"], "sizeY": w["sizeY"], "row": row, "col": 0}
        row += w["sizeY"]
    return {"main": {"widgets": cells,
                     "gridSettings": {"backgroundColor": "#eeeeee", "columns": 24,
                                      "margin": 10, "outerMargin": True,
                                      "backgroundSizeMode": "100%"}}}


def build():
    light_on = rpc_action("Bật đèn", "lightbulb", "setLight", True, "đã bật đèn")
    light_off = rpc_action("Tắt đèn", "lightbulb_outline", "setLight", False, "đã tắt đèn")
    ping = rpc_action("Ping", "network_ping", "ping", {}, "đã trả lời, RSSI/SNR sẽ cập nhật sau STATUS")
    test_on = rpc_action("Test 30 phút", "science", "setTestMode", 30, "đã bật test 30 phút (STATUS mỗi 90 s)")
    test_off = rpc_action("Tắt test", "stop_circle", "setTestMode", 0, "đã tắt test")

    operation = table(
        "operation", "Vận hành — danh sách Node",
        [key("online", "Online"), key("lightState", "Đèn"), key("mode", "Chế độ (1=remote)"),
         key("lux", "Lux"), key("healthFlags", "Health")],
        [light_on, light_off],
        [nav_action("Sang Test nhanh", "science", "test")])
    test_table = table(
        "test", "Test nhanh — tín hiệu từng Node",
        [key("online", "Online"), key("rssi", "RSSI", 0, "dBm"), key("snr", "SNR", 1, "dB"),
         key("testMode", "Test mode"), key("lightState", "Đèn")],
        [ping, test_on, test_off, light_on, light_off],
        [nav_action("Về Vận hành", "dashboard", "default")])
    signal_chart = chart("signal", "RSSI / SNR theo thời gian (1 giờ)")

    widgets = [operation, test_table, signal_chart]
    return {
        "title": "Hybrid-LoRa",
        "name": "Hybrid-LoRa",
        "configuration": {
            "description": "Generated by scripts/make_dashboard.py",
            "widgets": {w["id"]: w for w in widgets},
            "states": {
                "default": {"name": "Vận hành", "root": True, "layouts": layout([operation])},
                "test": {"name": "Test nhanh", "root": False,
                         "layouts": layout([test_table, signal_chart])},
            },
            "entityAliases": {
                ALIAS_ID: {"id": ALIAS_ID, "alias": "Tất cả Node",
                           "filter": {"type": "entityName", "resolveMultiple": True,
                                      "entityType": "DEVICE", "entityNameFilter": "Node-"}},
            },
            "filters": {},
            "timewindow": {"displayValue": "", "selectedTab": 0,
                           "realtime": {"realtimeType": 0, "interval": 1000,
                                        "timewindowMs": 3600000},
                           "aggregation": {"type": "NONE", "limit": 25000}},
            "settings": {"stateControllerId": "entity", "showTitle": True,
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
