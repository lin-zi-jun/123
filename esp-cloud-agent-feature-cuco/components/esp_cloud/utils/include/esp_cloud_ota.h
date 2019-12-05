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
#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <esp_cloud.h>

/** OTA Status to be reported to the ESP Cloud */
typedef enum {
    /** OTA is in Progress. This can be reported multiple times as the OTA progresses. */
    OTA_STATUS_IN_PROGRESS = 1,
    /** OTA Succeeded. This should be reported only once, at the end of OTA. */
    OTA_STATUS_SUCCESS,
    /** OTA Failed. This should be reported only once, at the end of OTA. */
    OTA_STATUS_FAILED,
    /** OTA was delayed by the application */
    OTA_STATUS_DELAYED,
} ota_status_t;

/** The OTA Handle to be used by the OTA callback */
typedef void *esp_cloud_ota_handle_t;

/** Function prototype for OTA Callback
 *
 * This function will be invoked by the ESP Cloud agent whenever an OTA is available.
 * The esp_cloud_report_ota_status() API should be used to indicate the progress and
 * success/fail status.
 *
 * @param[in] handle An OTA handle assigned by the ESP Cloud Agent
 * @param[in] url the OTA URL received from ESP Cloud
 * @param[in] prov_data The private data passed by the application to esp_cloud_enable_ota()
 *
 * @return ESP_OK if the OTA was successful
 * @return ESP_FAIL if the OTA failed.
 */
typedef esp_err_t (*esp_cloud_ota_callback_t) (esp_cloud_ota_handle_t handle, const char *url, void *ota_priv);

/** Enable OTA
 *
 * Calling this APE enabled OTA as per the ESP Cloud specification
 *
 * @param[in] handle The ESP Cloud handle
 * @param[in] ota_cb The callback to be invoked when an OTA Job is available
 * @param[in] prov_data Private data to be passed to the OTA callback
 *
 * @return ESP_OK on success
 * @return error on failure
 */
esp_err_t esp_cloud_enable_ota(esp_cloud_handle_t handle, esp_cloud_ota_callback_t ota_cb, void *ota_priv);

/** Report OTA Status
 *
 * This API must be called from the OTA Callback to indicate the status of the OTA. The OTA_STATUS_IN_PROGRESS
 * can be reported multiple times with appropriate additional information. The final success/failure should
 * be reported only once, at the end
 *
 * @param[in] ota_handle The OTA handle received by the callback
 * @param[in] status Status to be reported to ESP Cloud
 * @param[in] additional_info NULL terminated string indicating additional information for the status
 *
 * @return ESP_OK on success
 * @return error on failure
 */
esp_err_t esp_cloud_report_ota_status(esp_cloud_ota_handle_t ota_handle, ota_status_t status, char *additional_info);

esp_err_t app_publish_ota(char *url,int file_size,char * ota_version);

typedef struct _ota_status
{
    int type;
}_ota_status;

_ota_status ota_update_handle;

