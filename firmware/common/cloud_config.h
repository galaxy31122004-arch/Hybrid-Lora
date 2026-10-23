#pragma once

#if __has_include("cloud_config_local.h")
#include "cloud_config_local.h"
#endif

#ifndef CLOUD_WIFI_SSID
#define CLOUD_WIFI_SSID ""
#endif
#ifndef CLOUD_WIFI_PASSWORD
#define CLOUD_WIFI_PASSWORD ""
#endif
#ifndef CLOUD_ACCESS_TOKEN
#define CLOUD_ACCESS_TOKEN ""
#endif
#ifndef CLOUD_MQTT_HOST
#define CLOUD_MQTT_HOST "mqtt.thingsboard.cloud"
#endif
#ifndef CLOUD_ROOT_CA
#define CLOUD_ROOT_CA ""
#endif
