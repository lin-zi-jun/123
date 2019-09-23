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
#include <esp_log.h>
// #include <wifi_provisioning/manager.h>
#include <json_parser.h>
#include <json_generator.h>

#include "cloud.pb-c.h"
#include "esp_cloud_platform.h"

static const char *TAG = "esp_cloud_ota";

#define USER_ASSOC_TOPIC_SUFFIX "device/user/mapping"

typedef struct {
    char *user_id;
    char *secret_key;
} esp_cloud_user_assoc_data_t;
void esp_cloud_report_user_assoc(esp_cloud_handle_t handle, void *priv_data)
{
    esp_cloud_internal_handle_t *int_handle = (esp_cloud_internal_handle_t *)handle;
    esp_cloud_user_assoc_data_t *data = (esp_cloud_user_assoc_data_t *)priv_data;
    char publish_payload[150];
    json_str_t jstr;
    json_str_start(&jstr, publish_payload, sizeof(publish_payload), NULL, NULL);
    json_start_object(&jstr);
    json_obj_set_string(&jstr, "device_id", int_handle->device_id);
    json_obj_set_string(&jstr, "user_id", data->user_id);
    json_obj_set_string(&jstr, "secret_key", data->secret_key);
    json_end_object(&jstr);
    json_str_end(&jstr);
    char publish_topic[100];
    snprintf(publish_topic, sizeof(publish_topic), "%s/%s", int_handle->device_id, USER_ASSOC_TOPIC_SUFFIX);
    esp_err_t err = esp_cloud_platform_publish(int_handle, publish_topic, publish_payload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "User Assoc Publish Error %d", err);
    }
}

int esp_cloud_user_assoc_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    Cloud__CloudConfigPayload *data;
    data = cloud__cloud_config_payload__unpack(NULL, inlen, inbuf);

   


    if (!data) {
        ESP_LOGE(TAG, "No Data Received");
        return ESP_FAIL;
    }
    switch (data->msg) {
	case CLOUD__CLOUD_CONFIG_MSG_TYPE__TypeCmdGetSetDetails: {
	    ESP_LOGW(TAG, "Received request for device details");
	    Cloud__CloudConfigPayload resp;
	    Cloud__RespGetSetDetails payload;
	    cloud__cloud_config_payload__init(&resp);
	    cloud__resp_get_set_details__init(&payload);

        if (data->payload_case != CLOUD__CLOUD_CONFIG_PAYLOAD__PAYLOAD_CMD_GET_SET_DETAILS) {
            ESP_LOGE(TAG, "Invalid payload type in the message: %d", data->payload_case);
            payload.status = CLOUD__CLOUD_CONFIG_STATUS__InvalidParam;
        } else {
            ESP_LOGI(TAG, "Got user_id = %s, secret_key = %s", data->cmd_get_set_details->userid, data->cmd_get_set_details->secretkey);
            esp_cloud_user_assoc_data_t *user_assoc_data = calloc(1, sizeof(esp_cloud_user_assoc_data_t));
            if (!user_assoc_data) {
                /* TODO: Better status message like "CLOUD__CLOUD_CONFIG_STATUS__MemoryFull" */
                ESP_LOGI(TAG, "Sending status Invalid Param 1");
                payload.status = CLOUD__CLOUD_CONFIG_STATUS__InvalidParam;
            } else {
                user_assoc_data->user_id = strdup(data->cmd_get_set_details->userid);
                if (!user_assoc_data->user_id) {
                    ESP_LOGI(TAG, "Sending status Invalid Param 2");
                    payload.status = CLOUD__CLOUD_CONFIG_STATUS__InvalidParam;
                    free(user_assoc_data);
                } else {
                    user_assoc_data->secret_key = strdup(data->cmd_get_set_details->secretkey);
                    if (!user_assoc_data->user_id || !user_assoc_data->secret_key) {
                        ESP_LOGI(TAG, "Sending status Invalid Param 3");
                        free(user_assoc_data->user_id);
                        free(user_assoc_data);
                        payload.status = CLOUD__CLOUD_CONFIG_STATUS__InvalidParam;
                    } else {
                        ESP_LOGI(TAG, "Sending status SUCCESS");
                        payload.status = CLOUD__CLOUD_CONFIG_STATUS__Success;
                        payload.devicesecret = esp_cloud_get_device_id(esp_cloud_get_handle());
                        esp_cloud_queue_work(esp_cloud_get_handle(), esp_cloud_report_user_assoc, user_assoc_data);
                    }
                }
            }
        }
        resp.msg = CLOUD__CLOUD_CONFIG_MSG_TYPE__TypeRespGetSetDetails;
        resp.payload_case = CLOUD__CLOUD_CONFIG_PAYLOAD__PAYLOAD_RESP_GET_SET_DETAILS;
	    resp.resp_get_set_details = &payload;

	    *outlen = cloud__cloud_config_payload__get_packed_size(&resp);
	    *outbuf = (uint8_t *)malloc(*outlen);
	    cloud__cloud_config_payload__pack(&resp, *outbuf);
        // wifi_prov_mgr_stop_provisioning();
        break;
    }
    default:
	    ESP_LOGE(TAG, "Received invalid message type: %d", data->msg);
	    break;
    }
    cloud__cloud_config_payload__free_unpacked(data, NULL);
    return ESP_OK;
}
// esp_err_t esp_cloud_user_assoc_endpoint_create()
// {
//     esp_err_t err = wifi_prov_mgr_endpoint_create("cloud_user_assoc");
//     if (err == ESP_OK) {
//         wifi_prov_mgr_disable_auto_stop(2000);
//     }
//     return err;
// }

// esp_err_t esp_cloud_user_assoc_endpoint_register()
// {
//     return wifi_prov_mgr_endpoint_register("cloud_user_assoc", esp_cloud_user_assoc_handler, NULL);
// }
