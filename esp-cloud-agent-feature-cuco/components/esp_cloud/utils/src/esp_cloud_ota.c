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

#include "esp_cloud_mem.h"
#include "esp_cloud_internal.h"
#include "esp_cloud_platform.h"

static const char *TAG = "esp_cloud_ota";

#define OTAURL_TOPIC_SUFFIX     "device/otaurl"
#define OTAFETCH_TOPIC_SUFFIX   "device/otafetch"
#define OTASTATUS_TOPIC_SUFFIX  "device/otastatus"

typedef struct {
    esp_cloud_handle_t handle;
    esp_cloud_ota_callback_t ota_cb;
    void *ota_priv;
    char *ota_job_id;
    bool ota_in_progress;
    ota_status_t last_reported_status;
} esp_cloud_ota_t;

static esp_cloud_ota_t *esp_cloud_ota;

static char *ota_status_to_string(ota_status_t status)
{
    switch (status) {
        case OTA_STATUS_IN_PROGRESS:
            return "in-progress";
        case OTA_STATUS_SUCCESS:
            return "success";
        case OTA_STATUS_FAILED:
            return "failed";
        case OTA_STATUS_DELAYED:
            return "delayed";
        default:
            return "invalid";
    }
    return "invalid";
}

esp_err_t esp_cloud_report_ota_status(esp_cloud_ota_handle_t ota_handle, ota_status_t status, char *additional_info)
{
    if (!ota_handle) {
        return ESP_FAIL;
    }
    esp_cloud_ota_t *ota = (esp_cloud_ota_t *)ota_handle;
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)ota->handle;
    char publish_payload[200];
    json_str_t jstr;
    json_str_start(&jstr, publish_payload, sizeof(publish_payload), NULL, NULL);
    json_start_object(&jstr);
    json_obj_set_string(&jstr, "device_id", int_handle->device_id);
    json_obj_set_string(&jstr, "ota_job_id", ota->ota_job_id);
    json_obj_set_string(&jstr, "device_otastatus", ota_status_to_string(status));
    json_obj_set_string(&jstr, "additional_info", additional_info);
    json_end_object(&jstr);
    json_str_end(&jstr);

    char publish_topic[100];
    snprintf(publish_topic, sizeof(publish_topic), "%s/%s", int_handle->device_id, OTASTATUS_TOPIC_SUFFIX);
    esp_err_t err = esp_cloud_platform_publish(int_handle, publish_topic, publish_payload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_cloud_platform_publish_data returned error %d",err);
        return ESP_FAIL;
    }
    ota->last_reported_status = status;
    return ESP_OK;
}

extern int filesize;
static void ota_url_handler(const char *topic, void *payload, size_t payload_len, void *priv_data)
{
    if (!priv_data) {
        return;
    }
    esp_cloud_ota_handle_t ota_handle = priv_data;
    esp_cloud_ota_t *ota = (esp_cloud_ota_t *)ota_handle;
    if (ota->ota_in_progress) {
        return;
    }
    ota->ota_in_progress = true;
    /* Starting Firmware Upgrades */
    // ESP_LOGI(TAG, "Upgrade Handler got:%.*s on %s topic\n", (int) payload_len, (char *)payload, topic);
    ESP_LOGI(TAG, "Upgrade Handler got:%.*s\n", (int) payload_len, (char *)payload);
    /*
    {
    "url": "<fw_url>",
    "fwversion": "<fw_version>",
    "filesize": <size_in_bytes>
    }
    */
    jparse_ctx_t jctx;
    char *url = NULL, *ota_job_id = NULL;
    int ret = json_parse_start(&jctx, (char *)payload, (int) payload_len);
    if (ret != 0) {
        esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, "Aborted. JSON Payload error");
        ota->ota_in_progress = false;
        return;
    }
    int len = 0;
    ret = json_obj_get_strlen(&jctx, "ota_job_id", &len);
    if (ret != ESP_OK) {
        esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, "Aborted. OTA Updated ID not found in JSON");
        goto end;
    }
    len++; /* Increment for NULL character */
    ota_job_id = esp_cloud_mem_calloc(1, len);
    if (!ota_job_id) {
        esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, "Aborted. OTA Updated ID memory allocation failed");
        goto end;
    }
    json_obj_get_string(&jctx, "ota_job_id", ota_job_id, len);
    ESP_LOGI(TAG, "OTA Update ID: %s", ota_job_id);
    ota->ota_job_id = ota_job_id;
    len = 0;
    ret = json_obj_get_strlen(&jctx, "url", &len);
    if (ret != ESP_OK) {
        esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, "Aborted. URL not found in JSON");
        goto end;
    }
    len++; /* Increment for NULL character */
    url = esp_cloud_mem_calloc(1, len);
    if (!url) {
        esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, "Aborted. URL memory allocation failed");
        goto end;
    }
    json_obj_get_string(&jctx, "url", url, len);
    ESP_LOGI(TAG, "URL: %s", url);

    
    json_obj_get_int(&jctx, "file_size", &filesize);
    ESP_LOGI(TAG, "File Size: %d", filesize);

    json_parse_end(&jctx);

    esp_cloud_report_ota_status(ota_handle, OTA_STATUS_IN_PROGRESS, "Starting the Upgrade");
    esp_err_t err = ota->ota_cb((esp_cloud_ota_handle_t)ota, url, ota->ota_priv);
    if (err == ESP_OK) {
        if (ota->last_reported_status != OTA_STATUS_SUCCESS) {
            esp_cloud_report_ota_status(ota_handle, OTA_STATUS_SUCCESS, "Upgrade Finished");
        }
        esp_restart();
    }
    ESP_LOGE(TAG, "Firmware Upgrades Failed");
    /* We will come here only in case of error */
    free(url);
    if (ota->last_reported_status != OTA_STATUS_FAILED) {
        char description[50];
        snprintf(description, sizeof(description), "OTA failed with Error: %d", err);
        esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, description);
    }
    free(ota_job_id);
    ota->ota_job_id = NULL;
    ota->ota_in_progress = false;
    return;
end:
    if (url) {
        free(url);
    }
    if (ota_job_id) {
        free(ota_job_id);
        ota->ota_job_id = NULL;
    }
    json_parse_end(&jctx);
    ota->ota_in_progress = false;
    return;
}

static esp_err_t esp_cloud_ota_check(esp_cloud_handle_t handle, void *priv_data)
{
    char subscribe_topic[100];
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)handle;

    snprintf(subscribe_topic, sizeof(subscribe_topic),"%s/%s", int_handle->device_id, OTAURL_TOPIC_SUFFIX);

    ESP_LOGI(TAG, "Subscribing to: %s", subscribe_topic);
    /* First unsubscribing, in case there is a stale subscription */
    esp_cloud_platform_unsubscribe(int_handle, subscribe_topic);
    esp_err_t err = esp_cloud_platform_subscribe(int_handle, subscribe_topic, ota_url_handler, priv_data);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "OTA URL Subscription Error %d", err);
        return ESP_FAIL;
    }
    char publish_payload[150];
    json_str_t jstr;
    json_str_start(&jstr, publish_payload, sizeof(publish_payload), NULL, NULL);
    json_start_object(&jstr);
    json_obj_set_string(&jstr, "device_id", int_handle->device_id);
    json_obj_set_string(&jstr, "fw_version", int_handle->fw_version);
    json_end_object(&jstr);
    json_str_end(&jstr);
    char publish_topic[100];
    snprintf(publish_topic, sizeof(publish_topic), "%s/%s", int_handle->device_id, OTAFETCH_TOPIC_SUFFIX);
    err = esp_cloud_platform_publish(int_handle, publish_topic, publish_payload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA Fetch Publish Error %d", err);
    }
    return err;
}

#ifdef CONFIG_ESP_CLOUD_OTA_USE_DYNAMIC_PARAMS
static esp_err_t esp_cloud_ota_update_cb(const char *name, esp_cloud_param_val_t *param, void *priv_data)
{
    if(!param) {
        ESP_LOGI(TAG, "Empty value received for %s", name);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Delta - FW changed to %s", (char *) (param->val.s));

    esp_cloud_ota_t *ota = (esp_cloud_ota_t *)priv_data;
    esp_cloud_handle_t handle = ota->handle;
    esp_cloud_ota_check(handle, priv_data);
    /* Purposely sending ESP_FAIL here so that the received value is not reported back to cloud */
    return ESP_FAIL;
}
#else
static void esp_cloud_ota_work_fn(esp_cloud_handle_t handle, void *priv_data)
{
    esp_cloud_ota_check(handle, priv_data);
}
#endif
/* Enable the ESP Cloud specific OTA */
esp_err_t esp_cloud_enable_ota(esp_cloud_handle_t handle, esp_cloud_ota_callback_t ota_cb, void *ota_priv)
{
    if (!handle || !ota_cb || esp_cloud_ota) {
        return ESP_FAIL;
    }
    esp_cloud_ota = esp_cloud_mem_calloc(1, sizeof(esp_cloud_ota_t));
    if (!esp_cloud_ota) {
        return ESP_FAIL;
    }
    esp_cloud_ota->ota_cb = ota_cb;
    esp_cloud_ota->ota_priv = ota_priv;
    esp_cloud_ota->handle = handle;
#ifdef CONFIG_ESP_CLOUD_OTA_USE_DYNAMIC_PARAMS
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)handle;
    esp_err_t err =  esp_cloud_add_dynamic_string_param(int_handle, "fw_version", int_handle->fw_version, MAX_VERSION_STRING_LEN, esp_cloud_ota_update_cb, esp_cloud_ota);
#else
    esp_err_t err = esp_cloud_queue_work(handle, esp_cloud_ota_work_fn, esp_cloud_ota);
#endif
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA enabled");
    } else {
        ESP_LOGE(TAG, "Failed to enable OTA");
    }
    return err;
}

