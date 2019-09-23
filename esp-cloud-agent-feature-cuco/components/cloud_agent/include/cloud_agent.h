// Copyright 2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

/** Events which should sent in the callback. */
typedef enum cloud_agent_event {
    /* Init done should be sent when the cloud agent is connected to the cloud and is checking for an update. */
    CLOUD_AGENT_INIT_DONE,
    /* OTA start should be sent when the cloud agent has found an update and is about to start the update. */
    CLOUD_AGENT_OTA_START,
    /* OTA end should be sent when the cloud agent has completed the update and is about to reboot the device. */
    CLOUD_AGENT_OTA_END,
} cloud_agent_event_t;

/** Device details that might be useful for the cloud agent. */
typedef struct cloud_agent_device_cfg {
    /* Name of the device */
    char *name;
    /* Type of the device */
    char *type;
    /* Model of the device */
    char *model;
    /* Firmware version that the device is running */
    char *fw_version;
} cloud_agent_device_cfg_t;

/** OTA Start
This API needs to be implemented by the application.
This API should initialize the connection with the cloud and start checking for an update. It should be implemented in such a way that the device can check for an update again by calling this API.

The application should get a callback with CLOUD_AGENT_INIT_DONE after the connection has been made and the initialisation has been done. This might be useful if the application wants to check for an update only when this API is called and stop OTA checking few seconds after that by calling cloud_agent_ota_stop().
*/

void cloud_agent_ota_start(cloud_agent_device_cfg_t *cloud_agent_device_cfg, void (*cloud_agent_app_cb)(cloud_agent_event_t event));

/** OTA Stop
This API needs to be implemented by the application.
This API should stop checking for update from the cloud.
*/
void cloud_agent_ota_stop();
