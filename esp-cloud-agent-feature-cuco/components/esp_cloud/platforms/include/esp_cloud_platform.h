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
#include <esp_cloud.h>
#include <esp_cloud_internal.h>
typedef void (*esp_cloud_platform_subscribe_cb_t) (const char *topic, void *payload, size_t payload_len, void *priv_data);

esp_err_t esp_cloud_platform_init(esp_cloud_internal_handle_t *handle);
esp_err_t esp_cloud_platform_connect(esp_cloud_internal_handle_t *handle);
esp_err_t esp_cloud_platform_wait(esp_cloud_internal_handle_t *handle);
esp_err_t esp_cloud_platform_disconnect(esp_cloud_internal_handle_t *handle);

esp_err_t esp_cloud_platform_report_state(esp_cloud_internal_handle_t *handle);
esp_err_t esp_cloud_platform_register_dynamic_params(esp_cloud_internal_handle_t *handle);

esp_err_t esp_cloud_platform_publish(esp_cloud_internal_handle_t *handle, const char *topic, const char *data);
esp_err_t esp_cloud_platform_subscribe(esp_cloud_internal_handle_t *handle, const char *topic, esp_cloud_platform_subscribe_cb_t cb, void *priv_data);
esp_err_t esp_cloud_platform_unsubscribe(esp_cloud_internal_handle_t *handle, const char *topic);
