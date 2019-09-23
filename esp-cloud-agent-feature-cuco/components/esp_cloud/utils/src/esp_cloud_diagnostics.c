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
#include <stdint.h>
#include <json_parser.h>
#include <json_generator.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_cloud.h>
#include <esp_cloud_ota.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include "esp_cloud_mem.h"
#include "esp_cloud_internal.h"
#include "esp_cloud_platform.h"

static const char *TAG = "esp_cloud_diagnostics";

#define DIAGNOSTICS_TOPIC_SUFFIX     "device/diagnostics"

typedef struct esp_cloud_diag_entry {
    esp_cloud_work_fn_t work_fn;
    uint32_t period_seconds;
    TimerHandle_t timer;
    void *priv_data;
    struct esp_cloud_diag_entry *next;
} esp_cloud_diag_entry_t;


typedef struct {
    char *data;
    bool free_on_report;
} esp_cloud_diagnostics_data_t;

esp_cloud_diag_entry_t *esp_cloud_diag_first_entry;

static void esp_cloud_diagnostics_first_call(esp_cloud_handle_t handle, void *priv_data)
{
    if (!handle || !priv_data) {
        return;
    }
    esp_cloud_diag_entry_t *entry = (esp_cloud_diag_entry_t *)priv_data;
    esp_cloud_queue_work(handle, entry->work_fn, entry->priv_data);
    /* Start timer here so that the function is called periodically */
    xTimerStart(entry->timer, 0);
}

static esp_err_t esp_cloud_diagnostics_add_entry(esp_cloud_diag_entry_t *new_entry)
{
    esp_cloud_diag_entry_t *entry = esp_cloud_diag_first_entry;
    if (!entry) {
        esp_cloud_diag_first_entry = new_entry;
        return esp_cloud_queue_work(esp_cloud_get_handle(), esp_cloud_diagnostics_first_call, (void *)new_entry);
    }
    while (entry->next) {
        entry = entry->next;
    }
    entry->next = new_entry;
    return esp_cloud_queue_work(esp_cloud_get_handle(), esp_cloud_diagnostics_first_call, (void *)new_entry);
}

esp_err_t esp_cloud_diagnostics_send_data(esp_cloud_handle_t handle, char *data)
{
    if (!handle || !data) {
        return ESP_FAIL;
    }
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)handle;
    char publish_topic[100];

    snprintf(publish_topic, sizeof(publish_topic), "%s/%s", int_handle->device_id, DIAGNOSTICS_TOPIC_SUFFIX);
    esp_err_t err = esp_cloud_platform_publish(int_handle, publish_topic, data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_cloud_platform_publish_data returned error %d", err);
    }
    return err;
}

static void esp_cloud_diagnostics_send_data_queue_fn(esp_cloud_handle_t handle, void *priv_data)
{
    if (!handle || !priv_data) {
        return;
    }
    esp_cloud_diagnostics_data_t *diag_data = (esp_cloud_diagnostics_data_t *)priv_data;
    if (!diag_data->data) {
        free(diag_data);
        return;
    }
    esp_err_t err = esp_cloud_diagnostics_send_data(handle, diag_data->data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_cloud_diagnostics_send_data returned error %d", err);
    }
    if (diag_data->free_on_report) {
        free(diag_data->data);
    }
    free(diag_data);
}

esp_err_t esp_cloud_diagnostics_add_data(esp_cloud_handle_t handle, char *data, bool free_on_report)
{
    esp_cloud_diagnostics_data_t *diag_data = esp_cloud_mem_calloc(1, sizeof(esp_cloud_diagnostics_data_t));
    if (diag_data) {
        diag_data->data = data;
        diag_data->free_on_report = free_on_report;
        if (esp_cloud_queue_work(handle, esp_cloud_diagnostics_send_data_queue_fn, diag_data) == ESP_OK) {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static void esp_cloud_diagnostics_common_cb(TimerHandle_t handle)
{
    esp_cloud_diag_entry_t *entry = (esp_cloud_diag_entry_t *)pvTimerGetTimerID(handle);
    if (entry) {
        esp_cloud_queue_work(esp_cloud_get_handle(), entry->work_fn, entry->priv_data);
    }
}

esp_err_t esp_cloud_diagnostics_register_periodic_handler(esp_cloud_handle_t handle,
        esp_cloud_work_fn_t work_fn, uint32_t period_seconds, void *priv_data)
{
    if (!handle || !work_fn || (period_seconds == 0)) {
        return ESP_FAIL;
    }
    esp_cloud_diag_entry_t *diag_entry = esp_cloud_mem_calloc (1, sizeof(esp_cloud_diag_entry_t));
    if (!diag_entry) {
        return ESP_FAIL;
    }
    diag_entry->work_fn = work_fn;
    diag_entry->period_seconds = period_seconds;
    diag_entry->priv_data = priv_data;
    diag_entry->timer = xTimerCreate("test", (period_seconds * 1000)/ portTICK_PERIOD_MS, pdTRUE, (void *)diag_entry, esp_cloud_diagnostics_common_cb);
    if (!diag_entry->timer) {
        free(diag_entry);
        return ESP_FAIL;
    }
    return esp_cloud_diagnostics_add_entry(diag_entry);
}

