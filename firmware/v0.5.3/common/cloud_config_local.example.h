#pragma once

// Copy this file to cloud_config_local.h and fill in your own values.
#define CLOUD_ACCESS_TOKEN ""
#define CLOUD_MQTT_HOST "mqtt.thingsboard.cloud"
// PEM root CA, one quoted string per line ending in \n, joined with a
// backslash at the end of each line (a #define cannot hold a multi-line raw
// string). Leave it empty to keep ThingsBoard off. Example layout:
//   #define CLOUD_ROOT_CA                    (backslash)
//     "-----BEGIN CERTIFICATE-----\n"        (backslash)
//     "MIIC...\n"                            (backslash)
//     "-----END CERTIFICATE-----\n"
#define CLOUD_ROOT_CA ""
