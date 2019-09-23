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
/**
 * Register a periodic ESP Cloud diagnostics handler
 *
 * Any data to be reported by this handler should be managed by using esp_cloud_diagnostics_send_data().
 * Please ensure that the period is not too small causing unnecessary cloud communication.
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] diag_fn The diagnostics function to be registered
 * @param[in] period_seconds Periodicity for the function, in seconds. Value of 0 is not allowed.
 * @param[in] priv_data  Private data to be passed to the diagnostics function
 *
 * @return ESP_OK on success
 * @return error on failure
 */
esp_err_t esp_cloud_diagnostics_register_periodic_handler(esp_cloud_handle_t handle,
        esp_cloud_work_fn_t diag_fn, uint32_t period_seconds, void *priv_data);

/** Send Diagnostics Data
 *
 * This should be used only from the handler registered using esp_cloud_diagnostics_register_periodic_handler().
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] data NULL terminated data string to be reported
 *
 * @return ESP_OK on success
 * @return error on failure
 */
esp_err_t esp_cloud_diagnostics_send_data(esp_cloud_handle_t handle, char *data);

/** Add Diagnostics Data
 *
 * Add diagnostics data to be reported whenever by the ESP Cloud Agent
 *
 * This just queues the diagnostics data to be reported. Actual sending of the data is handled internally
 * within the ESP Cloud task
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] data NULL terminated data string to be reported
 * @param[in] free_on_report Set to true if you want the ESP Cloud agent to free the data after
 * it is reported to ESP Cloud.
 *
 * @return ESP_OK on success
 * @return error on failure
 */
esp_err_t esp_cloud_diagnostics_add_data(esp_cloud_handle_t handle, char *data, bool free_on_report);
