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
#include <string.h>
#include <json_parser.h>
#include <json_generator.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_cloud.h>
#include <esp_cloud_ota.h>

#include "esp_cloud_mem.h"
#include "esp_cloud_internal.h"
#include "esp_cloud_platform.h"
#include <esp_cloud_storage.h>
#include "app_auth_user.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"
static const char *TAG = "esp_cloud_ota";

#define OTAURL_TOPIC_SUFFIX     "device/otaurl"
#define OTAFETCH_TOPIC_SUFFIX   "device/otafetch"
#define OTASTATUS_TOPIC_SUFFIX  "device/otastatus"

typedef struct {
    esp_cloud_handle_t handle;
    esp_cloud_ota_callback_t ota_cb;
    void *ota_priv;
    char *ota_version;
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
    json_obj_set_int(&jstr, "ota_version", ota->ota_version);
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
                         
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)ota->handle;

    if (ota->ota_in_progress) {
        return;
    }
    ota->ota_in_progress = true;
    ESP_LOGI(TAG, "Upgrade Handler got:%.*s\n", (int) payload_len, (char *)payload);

    jparse_ctx_t jctx;
    char *url = NULL;
    int ret = json_parse_start(&jctx, (char *)payload, (int) payload_len);
    if (ret != 0) {
        // esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, "Aborted. JSON Payload error");
        ota_report_msg_status_val_to_app(false,"Aborted. JSON Payload error");
        ota->ota_in_progress = false;
        return;
    }

    int len = 0;
    ret = json_obj_get_strlen(&jctx, "ota_version", &len);
    if (ret != ESP_OK) {
        ota_report_msg_status_val_to_app(false,"Aborted. ota_version not found in JSON");
        // esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, "Aborted. URL not found in JSON");
        goto end;
    }
    len++; /* Increment for NULL character */
    ota->ota_version = esp_cloud_mem_calloc(1, len);

    ret = json_obj_get_string(&jctx, "ota_version", ota->ota_version,len);
     if (ret != ESP_OK) {
        ota_report_msg_status_val_to_app(false,"Aborted. ota_version not found in JSON");
        // esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, "Aborted. ota_version not found in JSON");
        goto end;
    }
    ota_report_progress_val_to_app(0);
    printf("remote:%s---cur%s\r\n",ota->ota_version,int_handle->fw_version);
    
    if(!strcmp(ota->ota_version,int_handle->fw_version)){
        ota_report_msg_status_val_to_app(true,"have update finish");
        report_device_info_to_server(OTA_UPDATE,ota_vertion,true,"have update finish");
        printf("have update finish\r\n");
        return;
    }else if(ota->ota_version[1]>=int_handle->fw_version[1]){
            if(ota->ota_version[3]>=int_handle->fw_version[3]){
                if(ota->ota_version[5]>=int_handle->fw_version[5]){
                    len = 0;
                    ret = json_obj_get_strlen(&jctx, "url", &len);
                    if (ret != ESP_OK) {
                        ota_report_msg_status_val_to_app(false,"Aborted. URL not found in JSON");
                        // esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, "Aborted. URL not found in JSON");
                        goto end;
                    }
                    len++; /* Increment for NULL character */
                    url = esp_cloud_mem_calloc(1, len);
                    if (!url) {
                        // esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, "Aborted. URL memory allocation failed");
                        ota_report_msg_status_val_to_app(false,"Aborted. URL memory allocation failed");
                        goto end;
                    }
                    json_obj_get_string(&jctx, "url", url, len);
                    ESP_LOGI(TAG, "URL: %s", url);

                    json_obj_get_int(&jctx, "file_size", &filesize);
                    ESP_LOGI(TAG, "File Size: %d", filesize);

                    json_parse_end(&jctx);

                    // esp_cloud_report_ota_status(ota_handle, OTA_STATUS_IN_PROGRESS, "Starting the Upgrade");
                    esp_err_t err = ota->ota_cb((esp_cloud_ota_handle_t)ota, url, ota->ota_priv);
                    if (err == ESP_OK) {
                        if (ota->last_reported_status != OTA_STATUS_SUCCESS) {
                            // esp_cloud_report_ota_status(ota_handle, OTA_STATUS_SUCCESS, "Upgrade Finished");
                            ota_report_msg_status_val_to_app(true,"have update finish");
                        }
                        esp_restart();
                    }
                    ESP_LOGE(TAG, "Firmware Upgrades Failed");
                    goto end;
                    /* We will come here only in case of error */
                    // free(url);
                    // if (ota->last_reported_status != OTA_STATUS_FAILED) {
                    //     char description[50];
                    //     snprintf(description, sizeof(description), "OTA failed with Error: %d", err);
                    //     ota_report_msg_status_val_to_app(true,description);
                    //     // esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, description);
                    // }

                    // ota->ota_in_progress = false;
                    // return;
                }else{
                    printf("update fail 3\r\n");
                    goto end;
                }
            }else{
                printf("update fail 2\r\n");
                goto end;
            }
    }else{
        printf("update fail 1\r\n");
        goto end;
    }
    
end: 
    ota_report_msg_status_val_to_app(false,"fail");
    if (url) {
        free(url);
    }
    json_parse_end(&jctx);
    ota->ota_in_progress = false;
     vTaskDelay(100/ portTICK_PERIOD_MS);
    esp_restart();
    return;
}

esp_cloud_internal_handle_t *int_app_handle;
static esp_err_t esp_cloud_ota_check(esp_cloud_handle_t handle, void *priv_data)
{
    char subscribe_topic[100]={0};
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)handle;
    int_app_handle = (esp_cloud_internal_handle_t *)handle;
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
    char publish_topic[100]={0};
    snprintf(publish_topic, sizeof(publish_topic), "%s/%s", int_handle->device_id, OTAFETCH_TOPIC_SUFFIX);
    err = esp_cloud_platform_publish(int_handle, publish_topic, publish_payload);
    if (err != ESP_OK) {                                                            
        ESP_LOGE(TAG, "OTA Fetch Publish Error %d", err);
    }
    return err;                                                                                                                                                             
}

esp_err_t app_publish_ota(char *url,int file_size,char * ota_version){
    char publish_payload[200];
    json_str_t jstr;
    json_str_start(&jstr, publish_payload, sizeof(publish_payload), NULL, NULL);
    json_start_object(&jstr);
    json_obj_set_string(&jstr, "url",url);
    json_obj_set_int(&jstr, "file_size", file_size);
     json_obj_set_string(&jstr, "ota_version",ota_version);
    json_end_object(&jstr);
    json_str_end(&jstr);
    char publish_topic[100]={0};
    snprintf(publish_topic, sizeof(publish_topic), "%s/%s", int_app_handle->device_id, OTAURL_TOPIC_SUFFIX);
    esp_err_t err = esp_cloud_platform_publish(int_app_handle, publish_topic, publish_payload);
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

