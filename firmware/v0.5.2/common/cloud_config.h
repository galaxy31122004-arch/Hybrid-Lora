#pragma once

#if __has_include("cloud_config_local.h")
#include "cloud_config_local.h"
#endif

// Wi-Fi is not configured here: networks come from include/wifi_secrets.h
// (generated, gitignored) and the board's NVS, see src/wifi_nvs.cpp.
#ifndef CLOUD_ACCESS_TOKEN
#define CLOUD_ACCESS_TOKEN ""
#endif
#ifndef CLOUD_MQTT_HOST
#define CLOUD_MQTT_HOST "mqtt.thingsboard.cloud"
#endif
#ifndef CLOUD_ROOT_CA
#define CLOUD_ROOT_CA ""
#endif
