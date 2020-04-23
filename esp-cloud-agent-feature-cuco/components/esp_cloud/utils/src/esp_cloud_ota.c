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
#include "user_auth.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"
#include "app_prov_handlers.h"
#include "user_ota.h"
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

static esp_err_t ota_url_handler(const char *topic, void *payload, size_t payload_len, void *priv_data)
{
    bool update_flag=false;
    int R_Main_version = 0, R_feature_version = 0,R_fix_version = 0;
    int C_Main_version = 0,C_feature_version = 0,C_fix_version = 0;
    if (!priv_data) {
        return ESP_FAIL;
    }
    esp_cloud_ota_handle_t ota_handle = priv_data;
    esp_cloud_ota_t *ota = (esp_cloud_ota_t *)ota_handle;
                         
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)ota->handle;

    if (ota->ota_in_progress) {
        return ESP_FAIL;
    }
    ota->ota_in_progress = true;
    ESP_LOGI(TAG, "Upgrade Handler got:%.*s\n", (int) payload_len, (char *)payload);

    jparse_ctx_t jctx;
    char *url = NULL;
    int ret = json_parse_start(&jctx, (char *)payload, (int) payload_len);
    if (ret != 0) {
        user_ota.ota_status = JCTX_ERR;
        ota->ota_in_progress = false;
        return ESP_FAIL;
    }

    int len = 0;
    ret = json_obj_get_strlen(&jctx, "ota_version", &len);
    if (ret != ESP_OK) {
        user_ota.ota_status = VERSION_ERR;
        return ESP_FAIL;
    }
    len++; /* Increment for NULL character */
    ota->ota_version = esp_cloud_mem_calloc(1, len);

    ret = json_obj_get_string(&jctx, "ota_version", ota->ota_version,len);
     if (ret != ESP_OK) {
         user_ota.ota_status = VERSION_ERR;
        return ESP_FAIL;
    }
    // ota_report_progress_val_to_app(0);
    printf("remote:%s---cur:%s\r\n",ota->ota_version,int_handle->fw_version);
    int n = sscanf(ota->ota_version, "%d.%d.%d",&R_Main_version,&R_feature_version,&R_fix_version);
    if(n!=3){
        user_ota.ota_status = VERSION_ERR;
        printf("version error\r\n");
        return ESP_FAIL;
    }
    n = sscanf(int_handle->fw_version, "%d.%d.%d",&C_Main_version,&C_feature_version,&C_fix_version);
    if(n!=3){
        user_ota.ota_status = VERSION_ERR;
        printf("version error\r\n");
        return ESP_FAIL;
    }

    if(!strcmp(ota->ota_version,int_handle->fw_version)){
        user_ota.ota_status = OTA_FINISH;
        user_bind_report(OTA_UPDATE,APP_TYPE,ota->ota_version,true,"have update finish");
        printf("have update finish\r\n");
        ota->ota_in_progress = false;
        return ESP_OK;

    }else if(R_Main_version>C_Main_version){
          update_flag=true;
    }else if(R_Main_version==C_Main_version){
            if(R_feature_version>C_feature_version){
                update_flag=true;
            }else if(R_feature_version==C_feature_version){

                if(R_fix_version>C_fix_version){
                    update_flag=true;
                }else{
                    printf("update fail 3\r\n");
                    user_ota.ota_status = VERSION_ERR;
                    return ESP_FAIL;
                }
            }else{
                printf("update fail 2\r\n");
                user_ota.ota_status = VERSION_ERR;
                return ESP_FAIL;
            }
    }
    else{
        printf("update fail 1\r\n");
        user_ota.ota_status = VERSION_ERR;
        return ESP_FAIL;
    }

    if(update_flag == true){
        update_flag=false;  
        len = 0;
        ret = json_obj_get_strlen(&jctx, "url", &len);
        if (ret != ESP_OK) {
            user_ota.ota_status = URL_ERR;
            return ESP_FAIL;
        }
        len++; /* Increment for NULL character */
        url = esp_cloud_mem_calloc(1, len);
        if (!url) {
             user_ota.ota_status = URL_MEM_ERR;
             return ESP_FAIL;
        }
        json_obj_get_string(&jctx, "url", url, len);
        ESP_LOGI(TAG, "URL: %s", url);

        json_parse_end(&jctx);

        esp_err_t err = ota->ota_cb((esp_cloud_ota_handle_t)ota, url, ota->ota_priv);
        if (err == ESP_OK) {
            user_ota.ota_status = OTA_FINISH;
        }
        ESP_LOGE(TAG, "Firmware Upgrades Failed");
        user_ota.ota_status = OTA_FAIL;
    }

    if (url) {
        free(url);
    }
    json_parse_end(&jctx);
    ota->ota_in_progress = false;
    return ESP_OK;
}

esp_cloud_internal_handle_t *int_app_handle;
esp_err_t esp_cloud_ota_check(esp_cloud_handle_t handle, void *priv_data)
{
    char subscribe_topic[100]={0};
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)handle;
    int_app_handle = (esp_cloud_internal_handle_t *)handle;
    snprintf(subscribe_topic, sizeof(subscribe_topic),"%s/%s", int_handle->device_id, OTAURL_TOPIC_SUFFIX);

    /* First unsubscribing, in case there is a stale subscription */
    esp_cloud_platform_unsubscribe(int_handle, subscribe_topic);
    esp_err_t err = esp_cloud_platform_subscribe(int_handle, subscribe_topic, ota_url_handler, priv_data);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "OTA URL Subscription Error %d", err);
        return ESP_FAIL;
    }

    alexa_and_user_config.ota_topic_sub_states = OTA_TOPIC_SUB_OK;

    uint8_t ota_flag = prov_hal.custom_config_storage_get_u8("OTA_F");
    if(ota_flag == CUSTOM_INVALID){
        prov_hal.custom_config_storage_set_u8("OTA_F",CUSTOM_INIT);
        ESP_LOGI(TAG,"flag CUSTOM_INIT:%d\r\n",CUSTOM_INIT);

    }else if(ota_flag == OTA_FAIL){
        user_bind_report(OTA_UPDATE,SERVER_TYPE,int_handle->fw_version,false,"Force fail");
        prov_hal.custom_config_storage_set_u8("OTA_F",CUSTOM_INIT);
        ESP_LOGI(TAG,"flag OTA_FAIL:%d\r\n",OTA_FAIL);

    }else if(ota_flag == OTA_FINISH){
        user_bind_report(OTA_UPDATE,SERVER_TYPE,int_handle->fw_version,true,"Force Finished Successfully");
        prov_hal.custom_config_storage_set_u8("OTA_F",CUSTOM_INIT);
        ESP_LOGI(TAG,"flag OTA_FINISH:%d\r\n",OTA_FINISH);
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

