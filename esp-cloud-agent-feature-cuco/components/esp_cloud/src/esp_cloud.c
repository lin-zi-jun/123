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
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <json_parser.h>
#include <json_generator.h>

#include "esp_cloud_mem.h"
#include "esp_cloud.h"
#include "esp_cloud_time_sync.h"
#include "esp_cloud_storage.h"
#include "esp_cloud_platform.h"

static const char *TAG = "esp_cloud";

#define INFO_TOPIC_SUFFIX       "device/info"

#define DEFAULT_STATIC_PARAMS_COUNT         4
#define DEFAULT_DYNAMIC_PARAMS_COUNT        1
#define ESP_CLOUD_TASK_QUEUE_SIZE           8

#define DEV_FAMILY  "Outlets"
#define DEV_MODEL   "ESP-Outlet-1"

#define ESP_CLOUD_TASK_STACK  6 * 1024

esp_cloud_internal_handle_t *g_cloud_handle;
/* Initialize the Cloud by setting proper fields in the handle and allocating memory */
esp_err_t esp_cloud_init(esp_cloud_config_t *config, esp_cloud_handle_t *handle)
{
    if (g_cloud_handle) {
        return ESP_FAIL;
    }
    if (esp_cloud_storage_init() != ESP_OK) {
        return ESP_FAIL;
    }
    g_cloud_handle = esp_cloud_mem_calloc(1, sizeof(esp_cloud_internal_handle_t));
    g_cloud_handle->device_id = esp_cloud_storage_get("device_id");
    if (!g_cloud_handle->device_id) {
        free(g_cloud_handle);
        g_cloud_handle = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Device UUID %s", g_cloud_handle->device_id);

    g_cloud_handle->work_queue = xQueueCreate(ESP_CLOUD_TASK_QUEUE_SIZE, sizeof(esp_cloud_work_queue_entry_t));
    if (!g_cloud_handle->work_queue) {
        free(g_cloud_handle->device_id);
        free(g_cloud_handle);
        g_cloud_handle = NULL;
        ESP_LOGE(TAG, "ESP Cloud Task Queue Creation Failed");
        return ESP_FAIL;
    }

    if (esp_cloud_platform_init(g_cloud_handle) != ESP_OK) {
        vQueueDelete(g_cloud_handle->work_queue);
        free(g_cloud_handle->device_id);
        free(g_cloud_handle);
        g_cloud_handle = NULL;
        return ESP_FAIL;
    }
    
    g_cloud_handle->max_dynamic_params_count = config->dynamic_cloud_params_count + DEFAULT_DYNAMIC_PARAMS_COUNT;
    g_cloud_handle->max_static_params_count = config->static_cloud_params_count + DEFAULT_STATIC_PARAMS_COUNT;
    g_cloud_handle->enable_time_sync = config->enable_time_sync;
    g_cloud_handle->reconnect_attempts = config->reconnect_attempts;
    g_cloud_handle->dynamic_cloud_params = esp_cloud_mem_calloc(g_cloud_handle->max_dynamic_params_count, sizeof(esp_cloud_dynamic_param_t));
    g_cloud_handle->static_cloud_params = esp_cloud_mem_calloc(g_cloud_handle->max_static_params_count, sizeof(esp_cloud_static_param_t));
    *handle = (esp_cloud_handle_t)g_cloud_handle;
    esp_cloud_add_static_string_param(*handle, "name", config->id.name);
    esp_cloud_add_static_string_param(*handle, "type", config->id.type);
    esp_cloud_add_static_string_param(*handle, "model", config->id.model);
    esp_cloud_add_static_string_param(*handle, "fw_version", config->id.fw_version);
    g_cloud_handle->fw_version = strdup(config->id.fw_version);
    return ESP_OK;
}

/* Internal. Add a generic new Dynamic Cloud Parameter */
static esp_cloud_static_param_t *esp_cloud_add_static_param(esp_cloud_handle_t handle, const char *name)
{
    if (!handle) {
        return NULL;
    }
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)handle;
    if (int_handle->cur_static_params_count == int_handle->max_static_params_count) {
        return NULL;
    }
    int i;
    esp_cloud_static_param_t *param = &int_handle->static_cloud_params[0];
    for (i = 0; i < int_handle->cur_static_params_count; i++, param++) {
        if (strcmp(name, param->name) == 0) {
            return NULL;
        }
    }
    param->name = strdup(name);
    int_handle->cur_static_params_count++;
    return param;
}

/* Add a Dynamic String Paramter */
esp_err_t esp_cloud_add_static_string_param(esp_cloud_handle_t handle, const char *name, const char *val)
{
    esp_cloud_static_param_t *param = esp_cloud_add_static_param(handle, name);
    if (!param) {
        return ESP_FAIL;
    }
    param->val.type = CLOUD_PARAM_TYPE_STRING;
    param->val.val.s = strdup(val);
    if (!param->val.val.s) {
        return ESP_ERR_NO_MEM;
    }
    param->val.val_size = strlen(val);
    return ESP_OK;
}

/* Add a Dynamic Integer Parameter */
esp_err_t esp_cloud_add_static_int_param(esp_cloud_handle_t handle, const char *name, int val)
{
    esp_cloud_static_param_t *param = esp_cloud_add_static_param(handle, name);
    if (!param) {
        return ESP_FAIL;
    }
    param->val.type = CLOUD_PARAM_TYPE_INTEGER;
    param->val.val.i = val;
    param->val.val_size = sizeof(int);
    return ESP_OK;
}

/* Add a Dynamic Float Parameter */
esp_err_t esp_cloud_add_static_float_param(esp_cloud_handle_t handle, const char *name, float val)
{
    esp_cloud_static_param_t *param = esp_cloud_add_static_param(handle, name);
    if (!param) {
        return ESP_FAIL;
    }
    param->val.type = CLOUD_PARAM_TYPE_FLOAT;
    param->val.val.f = val;
    param->val.val_size = sizeof(float);
    return ESP_OK;
}

/* Add a Dynamic Boolean Parameter */
esp_err_t esp_cloud_add_static_bool_param(esp_cloud_handle_t handle, const char *name, bool val)
{
    esp_cloud_static_param_t *param = esp_cloud_add_static_param(handle, name);
    if (!param) {
        return ESP_FAIL;
    }
    param->val.type = CLOUD_PARAM_TYPE_BOOLEAN;
    param->val.val.b = val;
    param->val.val_size = sizeof(bool);
    return ESP_OK;
}
/* Internal. Add a generic new Dynamic Cloud Parameter */
static esp_cloud_dynamic_param_t *esp_cloud_add_dynamic_param(esp_cloud_handle_t handle, const char *name, esp_cloud_param_callback_t cb, void *priv_data)
{
    if (!handle) {
        return NULL;
    }
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)handle;
    if (int_handle->cur_dynamic_params_count == int_handle->max_dynamic_params_count) {
        return NULL;
    }
    int i;
    esp_cloud_dynamic_param_t *param = &int_handle->dynamic_cloud_params[0];
    for (i = 0; i < int_handle->cur_dynamic_params_count; i++, param++) {
        if (strcmp(name, param->name) == 0) {
            return NULL;
        }
    }
    param->name = strdup(name);
    param->cb = cb;
    param->priv_data = priv_data;
    int_handle->cur_dynamic_params_count++;
    return param;
}

/* Add a Dynamic String Paramter */
esp_err_t esp_cloud_add_dynamic_string_param(esp_cloud_handle_t handle, const char *name, const char *val, size_t val_size, esp_cloud_param_callback_t cb, void *priv_data)
{
    esp_cloud_dynamic_param_t *param = esp_cloud_add_dynamic_param(handle, name, cb, priv_data);
    if (!param) {
        return ESP_FAIL;
    }
    param->val.type = CLOUD_PARAM_TYPE_STRING;
    param->val.val.s = strdup(val);
    if (!param->val.val.s) {
        return ESP_ERR_NO_MEM;
    }
    param->val.val_size = val_size;
    return ESP_OK;
}

/* Add a Dynamic Integer Parameter */
esp_err_t esp_cloud_add_dynamic_int_param(esp_cloud_handle_t handle, const char *name, int val, esp_cloud_param_callback_t cb, void *priv_data)
{
    esp_cloud_dynamic_param_t *param = esp_cloud_add_dynamic_param(handle, name, cb, priv_data);
    if (!param) {
        return ESP_FAIL;
    }
    param->val.type = CLOUD_PARAM_TYPE_INTEGER;
    param->val.val.i = val;
    param->val.val_size = sizeof(int);
    return ESP_OK;
}

/* Add a Dynamic Float Parameter */
esp_err_t esp_cloud_add_dynamic_float_param(esp_cloud_handle_t handle, const char *name, float val, esp_cloud_param_callback_t cb, void *priv_data)
{
    esp_cloud_dynamic_param_t *param = esp_cloud_add_dynamic_param(handle, name, cb, priv_data);
    if (!param) {
        return ESP_FAIL;
    }
    param->val.type = CLOUD_PARAM_TYPE_FLOAT;
    param->val.val.f = val;
    param->val.val_size = sizeof(float);
    return ESP_OK;
}

/* Add a Dynamic Boolean Parameter */
esp_err_t esp_cloud_add_dynamic_bool_param(esp_cloud_handle_t handle, const char *name, bool val, esp_cloud_param_callback_t cb, void *priv_data)
{
    esp_cloud_dynamic_param_t *param = esp_cloud_add_dynamic_param(handle, name, cb, priv_data);
    if (!param) {
        return ESP_FAIL;
    }
    param->val.type = CLOUD_PARAM_TYPE_BOOLEAN;
    param->val.val.b = val;
    param->val.val_size = sizeof(bool);
    return ESP_OK;
}

/* Get dynamic cloud param from name */
esp_cloud_dynamic_param_t *esp_cloud_get_dynamic_param_by_name(const char *name)
{
    if (!name) {
        return NULL;
    }
    int i;
    esp_cloud_dynamic_param_t *param = &g_cloud_handle->dynamic_cloud_params[0];
    for (i = 0; i < g_cloud_handle->cur_dynamic_params_count; i++, param++) {
        if (strcmp(name, param->name) == 0) {
            return param;
        }
    }
    return NULL;
}

static esp_cloud_dynamic_param_t *esp_cloud_get_dynamic_param_by_name_and_type(const char *name, esp_cloud_param_val_type_t param_type)
{
    if (!name) {
        return NULL;
    }
    int i;
    esp_cloud_dynamic_param_t *param = &g_cloud_handle->dynamic_cloud_params[0];
    for (i = 0; i < g_cloud_handle->cur_dynamic_params_count; i++, param++) {
        if (strcmp(name, param->name) == 0) {
            if (param->val.type == param_type) {
                return param;
            }
        }
    }
    return NULL;
}


/* TODO: Use Handle */
esp_err_t esp_cloud_update_bool_param(esp_cloud_handle_t handle, const char *name, bool val)
{
    esp_cloud_dynamic_param_t *param = esp_cloud_get_dynamic_param_by_name_and_type(name, CLOUD_PARAM_TYPE_BOOLEAN);
    if (param) {
        param->val.val.b = val;
        param->flags |= CLOUD_PARAM_FLAG_LOCAL_CHANGE;
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t esp_cloud_update_int_param(esp_cloud_handle_t handle, const char *name, int val)
{

    esp_cloud_dynamic_param_t *param = esp_cloud_get_dynamic_param_by_name_and_type(name, CLOUD_PARAM_TYPE_INTEGER);
    if (param) {
        param->val.val.i = val;
        param->flags |= CLOUD_PARAM_FLAG_LOCAL_CHANGE;
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t esp_cloud_update_float_param(esp_cloud_handle_t handle, const char *name, float val)
{
    esp_cloud_dynamic_param_t *param = esp_cloud_get_dynamic_param_by_name_and_type(name, CLOUD_PARAM_TYPE_FLOAT);
    if (param) {
        param->val.val.f = val;
        param->flags |= CLOUD_PARAM_FLAG_LOCAL_CHANGE;
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t esp_cloud_update_string_param(esp_cloud_handle_t handle, const char *name, char *val)
{
    esp_cloud_dynamic_param_t *param = esp_cloud_get_dynamic_param_by_name_and_type(name, CLOUD_PARAM_TYPE_INTEGER);
    if (param) {
        if (param->val.val.s) {
            free(param->val.val.s);
        }
        param->val.val.s = strdup(val);
        if (param->val.val.s) {
            param->flags |= CLOUD_PARAM_FLAG_LOCAL_CHANGE;
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static void esp_cloud_report_static_params(esp_cloud_internal_handle_t *handle, json_str_t *jptr)
{
    int i;
    esp_cloud_static_param_t *param = &handle->static_cloud_params[0];
    for (i = 0; i < handle->cur_static_params_count; i++, param++) {
        switch (param->val.type) {
            case CLOUD_PARAM_TYPE_BOOLEAN:
                json_obj_set_bool(jptr, param->name, param->val.val.b);
                break;
            case CLOUD_PARAM_TYPE_INTEGER:
                json_obj_set_int(jptr, param->name, param->val.val.i);
                break;
            case CLOUD_PARAM_TYPE_FLOAT:
                json_obj_set_float(jptr, param->name, param->val.val.f);
                break;
            case CLOUD_PARAM_TYPE_STRING:
                json_obj_set_string(jptr, param->name, param->val.val.s);
                break;
            default:
                break;
        }
    }
}

static esp_err_t esp_cloud_report_device_info(esp_cloud_internal_handle_t *handle)
{
    if (!handle) {
        return ESP_FAIL;
    }

    char publish_payload[200];
    json_str_t jstr;
    json_str_start(&jstr, publish_payload, sizeof(publish_payload), NULL, NULL);
    json_start_object(&jstr);
    json_obj_set_string(&jstr, "device_id", handle->device_id);
    esp_cloud_report_static_params(handle, &jstr);
    json_end_object(&jstr);
    json_str_end(&jstr);
    char publish_topic[100];
    snprintf(publish_topic, sizeof(publish_topic), "%s/%s", handle->device_id, INFO_TOPIC_SUFFIX);

    return esp_cloud_platform_publish(handle, publish_topic, publish_payload);

}

static esp_err_t esp_cloud_report_user_bind_info(esp_cloud_internal_handle_t *handle)
{
    if (!handle) {
        return ESP_FAIL;
    }

    char publish_payload[300];
    json_str_t jstr;
    json_str_t jstr_s;
    json_str_start(&jstr, publish_payload, sizeof(publish_payload), NULL, NULL);
    json_start_object(&jstr);
    json_obj_set_string(&jstr, "cmd", "notify");
    json_obj_set_string(&jstr, "source","device");
    json_push_object(&jstr,"result");
    json_obj_set_string(&jstr, "device_id", handle->device_id);
    json_obj_set_string(&jstr, "func","bind");
    json_obj_set_int(&jstr,"code",200);
    json_obj_set_string(&jstr, "msg","Binding success");
    json_pop_object(&jstr);
    // esp_cloud_report_static_params(handle, &jstr);
    json_end_object(&jstr);
    json_str_end(&jstr);
    char publish_topic[100];
    snprintf(publish_topic, sizeof(publish_topic), "%s/%s","app", "5ee82dd919b8411db088ed451c5c9e50");

    return esp_cloud_platform_publish(handle, publish_topic, publish_payload);

}

esp_err_t esp_cloud_report_device_state(esp_cloud_internal_handle_t *handle)
{
    return esp_cloud_platform_report_state(handle);
}

void esp_cloud_handle_work_queue(esp_cloud_internal_handle_t *handle)
{
    if (!handle) {
        return;
    }
    esp_cloud_work_queue_entry_t work_queue_entry;
    BaseType_t ret = xQueueReceive(handle->work_queue, &work_queue_entry, 0);
    while (ret == pdTRUE) {
        work_queue_entry.work_fn((esp_cloud_handle_t)handle, work_queue_entry.priv_data);
        ret = xQueueReceive(handle->work_queue, &work_queue_entry, 0);
    }
}
static void esp_cloud_task(void *param)
{
    if (!param) {
        return;
    }
    esp_cloud_internal_handle_t *handle = (esp_cloud_internal_handle_t *) param;

    if(handle->enable_time_sync) {
        esp_cloud_time_sync(); /* TODO: Error handling */
    }
    esp_err_t err = esp_cloud_platform_connect(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_cloud_platform_connect() returned %d. Aborting", err);
        vTaskDelete(NULL);
    }/* TODO: Error handling */
    esp_cloud_platform_register_dynamic_params(handle); /* TODO: Error handling */
    esp_cloud_report_device_info(handle);
    esp_cloud_report_device_state(handle);
    esp_cloud_report_user_bind_info(handle);
    while (!handle->cloud_stop) {
        esp_cloud_handle_work_queue(handle);
        esp_cloud_platform_wait(handle);
    }
    esp_cloud_platform_disconnect(handle);
    handle->cloud_stop = false;
    vTaskDelete(NULL);
}

esp_err_t esp_cloud_queue_work(esp_cloud_handle_t handle, esp_cloud_work_fn_t work_fn, void *priv_data)
{
    if (!handle) {
        return ESP_FAIL;
    }
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *) handle;
    esp_cloud_work_queue_entry_t work_queue_entry = {
        .work_fn = work_fn,
        .priv_data = priv_data,
    };
    if (xQueueSend(int_handle->work_queue, &work_queue_entry, 0) == pdTRUE) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

/* Start the Cloud */
esp_err_t esp_cloud_start(esp_cloud_handle_t handle)
{
    if (!handle) {
        return ESP_FAIL;
    }
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)handle;
    if (int_handle->enable_time_sync) {
        esp_cloud_time_sync_init();
    }

    ESP_LOGI(TAG, "Starting Cloud Agent");

    if (xTaskCreate(&esp_cloud_task, "esp_cloud_task", ESP_CLOUD_TASK_STACK, int_handle, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Couldn't create cloud task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t esp_cloud_stop(esp_cloud_handle_t handle)
{
    if (!handle) {
        return ESP_FAIL;
    }
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)handle;
    int_handle->cloud_stop = true;
    return ESP_OK;
}

esp_cloud_handle_t esp_cloud_get_handle()
{
    return (esp_cloud_handle_t)g_cloud_handle;
}

char *esp_cloud_get_device_id(esp_cloud_handle_t handle)
{
    if (!handle) {
        return NULL;
    }
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)handle;
    return int_handle->device_id;
}
