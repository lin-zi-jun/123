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
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

typedef struct {
    uint8_t flags;
    bool read_write;
    char *name;
    void *priv_data;
    esp_cloud_param_val_t val;
    esp_cloud_param_callback_t cb;
} esp_cloud_dynamic_param_t;

typedef struct {
    char *name;
    esp_cloud_param_val_t val;
} esp_cloud_static_param_t;

/* Handle to maintain internal information (will move to an internal file) */
typedef struct {
    char *device_id;
    char *fw_version;
    bool enable_time_sync;
    uint8_t max_dynamic_params_count;
    uint8_t cur_dynamic_params_count;
    esp_cloud_dynamic_param_t *dynamic_cloud_params;
    uint8_t max_static_params_count;
    uint8_t cur_static_params_count;
    esp_cloud_static_param_t *static_cloud_params;
    uint16_t reconnect_attempts;
    void *cloud_platform_priv;
    bool cloud_stop;
    QueueHandle_t work_queue;
} esp_cloud_internal_handle_t;

typedef struct {
    esp_cloud_work_fn_t work_fn;
    void *priv_data;
} esp_cloud_work_queue_entry_t;

esp_cloud_dynamic_param_t *esp_cloud_get_dynamic_param_by_name(const char *name);
#define CLOUD_PARAM_FLAG_LOCAL_CHANGE   0x01
#define CLOUD_PARAM_FLAG_REMOTE_CHANGE  0x02
