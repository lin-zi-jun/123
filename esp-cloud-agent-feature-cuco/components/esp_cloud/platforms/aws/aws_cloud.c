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
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include <aws_iot_config.h>
#include <aws_iot_log.h>
#include <aws_iot_version.h>
#include <aws_iot_mqtt_client_interface.h>
#include <aws_iot_shadow_interface.h>

#include <esp_cloud_mem.h>
#include <esp_cloud.h>
#include <esp_cloud_storage.h>

#include "esp_cloud_platform.h"
#include "aws_custom_utils.h"
#include "app_auth_user.h"
// #include "production_test.h"
#define MAX_LENGTH_OF_UPDATE_JSON_BUFFER 200
#define AWS_TASK_STACK  12 * 1024
static const char *TAG = "aws_cloud";


#define MFG_PARTITION_NAME "fctry"
#define MAX_MQTT_SUBSCRIPTIONS      3

typedef struct {
    char *topic;
    esp_cloud_platform_subscribe_cb_t cb;
    void *priv;
} aws_cloud_subscription_t;

typedef struct {
    AWS_IoT_Client mqttClient;
    char *mqtt_host;
    char *client_cert;
    char *client_key;
    char *server_cert;
    jsonStruct_t *dynamic_params;
    jsonStruct_t **desired_handles;
    jsonStruct_t **reported_handles;
    size_t reported_count;
    size_t desired_count;
    bool shadowUpdateInProgress;
    aws_cloud_subscription_t *subscriptions[MAX_MQTT_SUBSCRIPTIONS];
} aws_cloud_platform_data_t;

static void aws_common_subscribe_callback(AWS_IoT_Client *pClient, char *pTopicName, uint16_t topicNameLen, IoT_Publish_Message_Params *params, void *pClientData)
{
    if (!pClientData) {
        return;
    }
    aws_cloud_subscription_t **subscriptions = (aws_cloud_subscription_t **)pClientData;
    int i;
    for (i = 0; i < MAX_MQTT_SUBSCRIPTIONS; i++) {
        if (subscriptions[i]) {
            if (strncmp(pTopicName, subscriptions[i]->topic, topicNameLen) == 0) {
                subscriptions[i]->cb(pTopicName, params->payload, params->payloadLen, subscriptions[i]->priv);
            }
        }
    }
}

esp_err_t esp_cloud_platform_subscribe(esp_cloud_internal_handle_t *handle, const char *topic, esp_cloud_platform_subscribe_cb_t cb, void *priv_data)
{
    if (!handle || !topic || !cb || !handle->cloud_platform_priv) {
        return ESP_FAIL;
    }
    aws_cloud_platform_data_t *platform_data = handle->cloud_platform_priv;
    int i;
    for (i = 0; i < MAX_MQTT_SUBSCRIPTIONS; i++) {
        if (!platform_data->subscriptions[i]) {
            aws_cloud_subscription_t *subscription = esp_cloud_mem_calloc(1, sizeof(aws_cloud_subscription_t));
            if (!subscription) {
                return ESP_FAIL;
            }
            subscription->topic = strdup(topic);
            if (!subscription->topic) {
                free(subscription);
                return ESP_FAIL;
            }
            IoT_Error_t rc = aws_iot_mqtt_subscribe(&platform_data->mqttClient, subscription->topic, strlen(subscription->topic),
                        QOS1, aws_common_subscribe_callback, platform_data->subscriptions);
            if (rc != SUCCESS) {
                free(subscription->topic);
                free(subscription);
                return ESP_FAIL;
            }
            subscription->priv = priv_data;
            subscription->cb = cb;
            platform_data->subscriptions[i] = subscription;
            ESP_LOGI(TAG, "Subscribed to topic: %s", topic);
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

esp_err_t esp_cloud_platform_unsubscribe(esp_cloud_internal_handle_t *handle, const char *topic)
{
    if (!handle || !topic || !handle->cloud_platform_priv) {
        return ESP_FAIL;
    }
    aws_cloud_platform_data_t *platform_data = handle->cloud_platform_priv;
    IoT_Error_t rc = aws_iot_mqtt_unsubscribe(&platform_data->mqttClient, topic, strlen(topic));
    if (rc == SUCCESS) {
        ESP_LOGW(TAG, "Could not unsubscribe from topic: %s", topic);
    }
    aws_cloud_subscription_t **subscriptions = platform_data->subscriptions;
    int i;
    for (i = 0; i < MAX_MQTT_SUBSCRIPTIONS; i++) {
        if (subscriptions[i]) {
            if (strncmp(topic, subscriptions[i]->topic, strlen(topic)) == 0) {
                free(subscriptions[i]->topic);
                free(subscriptions[i]);
                subscriptions[i] = NULL;
                return ESP_OK;
            }
        }
    }
    return ESP_FAIL;
}

esp_err_t esp_cloud_platform_publish(esp_cloud_internal_handle_t *handle, const char *topic, const char *data)
{
    if (!handle || !topic || !data || !handle->cloud_platform_priv) {
        return ESP_FAIL;
    }
    aws_cloud_platform_data_t *platform_data = handle->cloud_platform_priv;
    IoT_Publish_Message_Params publish_msg;
    publish_msg.qos = QOS1;
    publish_msg.payload = (void *) data;
    publish_msg.payloadLen = strlen(data);
    publish_msg.isRetained = 0;
    ESP_LOGI(TAG, "Publishing to: %s", topic);
    ESP_LOGI(TAG, "Publish Data: %s", data);
    IoT_Error_t rc = aws_iot_mqtt_publish(&platform_data->mqttClient, topic, strlen(topic), &publish_msg);

    if (SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_mqtt_publish returned error %d, aborting...", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void aws_get_new_param_val(esp_cloud_param_val_t *param_val, const jsonStruct_t *received_val)
{
    switch(param_val->type) {
        case CLOUD_PARAM_TYPE_BOOLEAN:
            param_val->val.b = *(bool *) (received_val->pData);
            break;
        case CLOUD_PARAM_TYPE_INTEGER:
            param_val->val.i = *(int *) (received_val->pData);
            break;
        case CLOUD_PARAM_TYPE_FLOAT:
            param_val->val.f = *(float *) (received_val->pData);
            break;
        case CLOUD_PARAM_TYPE_STRING:
            param_val->val.s = (char *) (received_val->pData);
            break;
        default:
            ESP_LOGE(TAG, "aws_get_new_param_val got invalid value type");
        break;
    }
}

static void aws_common_delta_callback(const char* pJsonValueBuffer, uint32_t valueLength, jsonStruct_t *pContext)
{
    printf("aws_common_delta_callback------>\r\n");
    if(pContext == NULL) {
        return;
    }
    esp_cloud_dynamic_param_t *param = esp_cloud_get_dynamic_param_by_name(pContext->pKey);
    if (param) {
        if (param->cb) {
            esp_cloud_param_val_t new_val;
            new_val.type = param->val.type;
            new_val.val_size = param->val.val_size;
            aws_get_new_param_val(&new_val, pContext);
            if (param->cb(pContext->pKey, &new_val, param->priv_data) == ESP_OK) {
                param->flags |= CLOUD_PARAM_FLAG_REMOTE_CHANGE;
            }
        }
    }
}

static void update_status_callback(const char *pThingName, ShadowActions_t action, Shadow_Ack_Status_t status,
                                   const char *pReceivedJsonDocument, void *pContextData)
{
    IOT_UNUSED(pThingName);
    IOT_UNUSED(action);
    IOT_UNUSED(pReceivedJsonDocument);
    aws_cloud_platform_data_t *platform_data = (aws_cloud_platform_data_t *) pContextData;

    if (platform_data) {
        platform_data->shadowUpdateInProgress = false;
    }

    if (SHADOW_ACK_TIMEOUT == status) {
        ESP_LOGE(TAG, "Update timed out-1");
    } else if (SHADOW_ACK_REJECTED == status) {
        ESP_LOGE(TAG, "Update rejected");
    } else if (SHADOW_ACK_ACCEPTED == status) {
        ESP_LOGI(TAG, "Update accepted");
    }
}

static IoT_Error_t shadow_update(esp_cloud_internal_handle_t *handle)
{
    if (!handle || !handle->cloud_platform_priv) {
        return ESP_FAIL;
    }
    aws_cloud_platform_data_t *platform_data = handle->cloud_platform_priv;
    IoT_Error_t rc = FAILURE;

    char JsonDocumentBuffer[MAX_LENGTH_OF_UPDATE_JSON_BUFFER];
    size_t sizeOfJsonDocumentBuffer = sizeof(JsonDocumentBuffer) / sizeof(JsonDocumentBuffer[0]);
    rc = aws_iot_shadow_init_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
    if (rc != SUCCESS) {
        return rc;
    }

    if (platform_data->reported_count > 0) {
        rc = custom_aws_iot_shadow_add_reported(JsonDocumentBuffer,
                                                sizeOfJsonDocumentBuffer,
                                                platform_data->reported_count,
                                                platform_data->reported_handles);
        if (rc != SUCCESS) {
            return rc;
        }
    }

    if (platform_data->desired_count > 0) {
        rc = custom_aws_iot_shadow_add_desired(JsonDocumentBuffer,
                            sizeOfJsonDocumentBuffer,
                            platform_data->desired_count,
                            platform_data->desired_handles);
        if (rc != SUCCESS) {
            return rc;
        }
    }

    rc = aws_iot_finalize_json_document(JsonDocumentBuffer, sizeOfJsonDocumentBuffer);
    if (rc != SUCCESS) {
        return rc;
    }
    ESP_LOGI(TAG, "Update Shadow: %s", JsonDocumentBuffer);
    rc = aws_iot_shadow_update(&platform_data->mqttClient, handle->device_id, JsonDocumentBuffer,
                               update_status_callback, platform_data, 4, true);           
    platform_data->shadowUpdateInProgress = true;
    return rc;
}

static esp_err_t esp_cloud_param_map_to_aws(esp_cloud_dynamic_param_t *cloud_param, jsonStruct_t *aws_param)
{
    if (!cloud_param || !aws_param) {
        return ESP_FAIL;
    }
    switch(cloud_param->val.type) {
        case CLOUD_PARAM_TYPE_BOOLEAN: {
                aws_param->pData = esp_cloud_mem_malloc(sizeof(bool));
                *(bool *)aws_param->pData = cloud_param->val.val.b;
                aws_param->dataLength = cloud_param->val.val_size;
                aws_param->pKey = cloud_param->name;
                aws_param->type = SHADOW_JSON_BOOL;
            }
            break;
        case CLOUD_PARAM_TYPE_INTEGER: {
                aws_param->pData = esp_cloud_mem_malloc(sizeof(int));
                *(int *)aws_param->pData = cloud_param->val.val.i;
                aws_param->dataLength = cloud_param->val.val_size;
                aws_param->pKey = cloud_param->name;
                aws_param->type = SHADOW_JSON_INT32;
            }
            break;
        case CLOUD_PARAM_TYPE_FLOAT: {
                aws_param->pData = esp_cloud_mem_malloc(sizeof(float));
                *(float *)aws_param->pData = cloud_param->val.val.f;
                aws_param->dataLength = cloud_param->val.val_size;
                aws_param->pKey = cloud_param->name;
                aws_param->type = SHADOW_JSON_FLOAT;
            }
            break;
        case CLOUD_PARAM_TYPE_STRING: {
                aws_param->pData = esp_cloud_mem_calloc(1, cloud_param->val.val_size);
                strncpy((char *)aws_param->pData, cloud_param->val.val.s, cloud_param->val.val_size);
                aws_param->dataLength = cloud_param->val.val_size;
                aws_param->pKey = cloud_param->name;
                aws_param->type = SHADOW_JSON_STRING;
            }
            break;
        default :
            ESP_LOGW(TAG, "Invalid Cloud param value type. Skipping...");
            return ESP_FAIL;
    }
    return ESP_OK;
}

void disconnectCallbackHandler(AWS_IoT_Client *pClient, void *data)
{
    ESP_LOGW(TAG, "MQTT Disconnect");
    IoT_Error_t rc = FAILURE;
    if(NULL == pClient) {
        return;
    }

    if(aws_iot_is_autoreconnect_enabled(pClient)) {
        ESP_LOGI(TAG, "Auto Reconnect is enabled, Reconnecting attempt will start now");
    } else {
        ESP_LOGW(TAG, "Auto Reconnect not enabled. Starting manual reconnect...");
        rc = aws_iot_mqtt_attempt_reconnect(pClient);
        if(NETWORK_RECONNECTED == rc) {
            ESP_LOGW(TAG, "Manual Reconnect Successful");
        } else {
            ESP_LOGW(TAG, "Manual Reconnect Failed - %d", rc);
        }
    }
}

esp_err_t esp_cloud_platform_connect(esp_cloud_internal_handle_t *handle)
{
    if (!handle || !handle->cloud_platform_priv) {
        return ESP_FAIL;
    }
    aws_cloud_platform_data_t *platform_data = handle->cloud_platform_priv;
    IoT_Error_t rc = FAILURE;

    ShadowInitParameters_t sp = ShadowInitParametersDefault;
    sp.pHost = platform_data->mqtt_host;
    sp.port = AWS_IOT_MQTT_PORT;
    sp.pClientCRT = (const char *)platform_data->client_cert;
    sp.pClientKey = (const char *)platform_data->client_key;
    sp.pRootCA = (const char *)platform_data->server_cert;
    sp.enableAutoReconnect = true;
    sp.disconnectHandler = disconnectCallbackHandler;

    ESP_LOGI(TAG, "Shadow Init");
    rc = aws_iot_shadow_init(&platform_data->mqttClient, &sp);
    if (SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_shadow_init returned error %d, aborting...", rc);
        return ESP_FAIL;
    }

    ShadowConnectParameters_t scp = ShadowConnectParametersDefault;
    scp.pMyThingName = handle->device_id;
    scp.pMqttClientId = handle->device_id;
    scp.mqttClientIdLen = (uint16_t) strlen(handle->device_id);

    ESP_LOGI(TAG, "Connecting to AWS.....");
    // int reconnect_attempts = handle->reconnect_attempts ? handle->reconnect_attempts : 1;
    do {
        rc = aws_iot_shadow_connect(&platform_data->mqttClient, &scp);
        if(SUCCESS != rc) {
            ESP_LOGE(TAG, "Error(%d) connecting to %s:%d", rc, sp.pHost, sp.port);
            dev_states = IOT_FAIL;
            vTaskDelay(1000 / portTICK_RATE_MS);
        }else{
            dev_states = IOT_OK;
            ESP_LOGI(TAG, "connecting to %s:%d",sp.pHost, sp.port);
        }
        // if (handle->reconnect_attempts) {
        //     reconnect_attempts--;
        // }
    // } while(SUCCESS != rc && reconnect_attempts);
    } while(SUCCESS != rc);
    // if (reconnect_attempts == 0) {
    //     ESP_LOGE(TAG, "Could not connect to cloud even after %d attempts", handle->reconnect_attempts);
    //     return ESP_FAIL;
    // }
    return ESP_OK;
}

static void aws_unsubscribe_all(esp_cloud_internal_handle_t *handle)
{
    aws_cloud_platform_data_t *platform_data = handle->cloud_platform_priv;
    int i;
    for (i = 0; i < MAX_MQTT_SUBSCRIPTIONS; i++) {
        if (platform_data->subscriptions[i]) {
            esp_cloud_platform_unsubscribe(handle, platform_data->subscriptions[i]->topic);
        }
    }
}


static void aws_remove_all_dynamic_params(esp_cloud_internal_handle_t *handle)
{
    aws_cloud_platform_data_t *platform_data = handle->cloud_platform_priv;
    int i;
    for (i = 0; i < handle->cur_dynamic_params_count; i++) {
        if (platform_data->dynamic_params[i].type == SHADOW_JSON_STRING) {
            free(platform_data->dynamic_params[i].pData);
        }
    }
    if (platform_data->dynamic_params) {
        free(platform_data->dynamic_params);
        platform_data->dynamic_params = NULL;
    }
    if (platform_data->desired_handles) {
        free(platform_data->desired_handles);
        platform_data->desired_handles = NULL;
    }
    if (platform_data->reported_handles) {
        free(platform_data->reported_handles);
        platform_data->reported_handles = NULL;
    }
}
esp_err_t esp_cloud_platform_disconnect(esp_cloud_internal_handle_t *handle)
{
    if (!handle || !handle->cloud_platform_priv) {
        return ESP_FAIL;
    }
    aws_unsubscribe_all(handle);
    aws_remove_all_dynamic_params(handle);
    aws_cloud_platform_data_t *platform_data = handle->cloud_platform_priv;
    IoT_Error_t rc = aws_iot_shadow_disconnect(&platform_data->mqttClient);
    if (SUCCESS != rc) {
        ESP_LOGE(TAG, "Failed to disconnect from cloud");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "AWS Disconnected.");
    return ESP_OK;
}

esp_err_t esp_cloud_platform_register_dynamic_params(esp_cloud_internal_handle_t *handle)
{
    if (!handle || !handle->cloud_platform_priv) {
        return ESP_FAIL;
    }
    aws_cloud_platform_data_t *platform_data = handle->cloud_platform_priv;
    IoT_Error_t rc = FAILURE;

    if (handle->cur_dynamic_params_count == 0) {
        return ESP_OK;
    }
    jsonStruct_t *dynamic_params = esp_cloud_mem_calloc(handle->cur_dynamic_params_count, sizeof(jsonStruct_t));

    int i;
    printf("handle->cur_dynamic_params_count:%d--------------\r\n",handle->cur_dynamic_params_count);
    for (i = 0; i < handle->cur_dynamic_params_count; i++) {
        dynamic_params[i].cb = aws_common_delta_callback;
        esp_err_t err = esp_cloud_param_map_to_aws(&handle->dynamic_cloud_params[i], &dynamic_params[i]);
        if (err == ESP_OK && dynamic_params[i].cb) {
            rc = aws_iot_shadow_register_delta(&platform_data->mqttClient, &dynamic_params[i]);
            if(SUCCESS != rc) {
                ESP_LOGE(TAG, "Shadow Register Delta Error %d", rc);
                free(dynamic_params);
                return ESP_FAIL;
            }
        }
    }
    jsonStruct_t **desired_handles = esp_cloud_mem_calloc(handle->cur_dynamic_params_count, sizeof(jsonStruct_t *));
    if (desired_handles == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        free(dynamic_params);
        return ESP_FAIL;
    }

    jsonStruct_t **reported_handles = esp_cloud_mem_calloc(handle->cur_dynamic_params_count, sizeof(jsonStruct_t *));
    if (reported_handles == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        free(desired_handles);
        free(dynamic_params);
        return ESP_FAIL;
    }
    platform_data->dynamic_params = dynamic_params;
    platform_data->desired_handles = desired_handles;
    platform_data->reported_handles = reported_handles;
    return ESP_OK;
}

esp_err_t esp_cloud_platform_report_state(esp_cloud_internal_handle_t *handle)
{
    if (!handle || !handle->cloud_platform_priv) {
        return ESP_FAIL;
    }
    aws_cloud_platform_data_t *platform_data = handle->cloud_platform_priv;
    if (handle->cur_dynamic_params_count == 0) {
        return ESP_OK;
    }
    // Report the initial values once
    platform_data->reported_count = 0;
    int i;
    for (i = 0; i < handle->cur_dynamic_params_count; i++) {
        platform_data->reported_handles[platform_data->reported_count++] =
                &platform_data->dynamic_params[i];
    }
    printf("platform_data->reported_count:%d\n",platform_data->reported_count);
    shadow_update(handle);  
    while(platform_data->shadowUpdateInProgress) {
        aws_iot_shadow_yield(&platform_data->mqttClient, 1000);
    }
    return ESP_OK;
}
void get_new_value(esp_cloud_param_val_t *param_val, jsonStruct_t *dynamic_params)
{
    switch(param_val->type) {
        case CLOUD_PARAM_TYPE_BOOLEAN:
            * (bool *)dynamic_params->pData = param_val->val.b;
        break;
        default:
            ESP_LOGE(TAG, "aws_get_new_param_val got invalid value type");
        break;
    }
}
esp_err_t esp_cloud_platform_wait(esp_cloud_internal_handle_t *handle)
{
    if (!handle || !handle->cloud_platform_priv) {
        return ESP_FAIL;
    }
    aws_cloud_platform_data_t *platform_data = handle->cloud_platform_priv;
    IoT_Error_t rc = SUCCESS;
    while (1) {
        rc = aws_iot_shadow_yield(&platform_data->mqttClient, 200);
        if (NETWORK_ATTEMPTING_RECONNECT == rc || platform_data->shadowUpdateInProgress) {
           aws_iot_shadow_yield(&platform_data->mqttClient, 1000);
            // If the client is attempting to reconnect, or already waiting on a shadow update,
            // we will skip the rest of the loop.
            continue;
        }
        break;
    }
    platform_data->desired_count = 0;
    platform_data->reported_count = 0;
    int i;
    for (i = 0; i < handle->cur_dynamic_params_count; i++) {
        if (handle->dynamic_cloud_params[i].flags & CLOUD_PARAM_FLAG_LOCAL_CHANGE) {
            get_new_value(&handle->dynamic_cloud_params[i].val, &platform_data->dynamic_params[i]);
            platform_data->reported_handles[platform_data->reported_count++] =
                &platform_data->dynamic_params[i];
            platform_data->desired_handles[platform_data->desired_count++] =
                &platform_data->dynamic_params[i];
        } else if (handle->dynamic_cloud_params[i].flags & CLOUD_PARAM_FLAG_REMOTE_CHANGE) {
            platform_data->reported_handles[platform_data->reported_count++] =
                &platform_data->dynamic_params[i];

            platform_data->desired_handles[platform_data->desired_count++] =                //lin 2019-9-19
                &platform_data->dynamic_params[i];

        }
        handle->dynamic_cloud_params[i].flags = 0;
    }

    if (platform_data->reported_count > 0 || platform_data->desired_count > 0) {
        rc = shadow_update(handle);
    }
    return ESP_OK;
}

esp_err_t esp_cloud_platform_init(esp_cloud_internal_handle_t *handle)
{
    if (handle->cloud_platform_priv) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Initialising Cloud");
    aws_cloud_platform_data_t *platform_data = esp_cloud_mem_calloc(1, sizeof(aws_cloud_platform_data_t));
    if (!platform_data) {
        return ESP_FAIL;
    }
    if ((platform_data->mqtt_host = esp_cloud_storage_get("mqtt_host")) == NULL) {
        goto init_err;
    }
    if ((platform_data->client_cert = esp_cloud_storage_get("client_cert")) == NULL) {
        goto init_err;
    }
    if ((platform_data->client_key = esp_cloud_storage_get("client_key")) == NULL) {
        goto init_err;
    }
    if ((platform_data->server_cert = esp_cloud_storage_get("server_cert")) == NULL) {
        goto init_err;
    }
    handle->cloud_platform_priv = platform_data;
    return ESP_OK;

init_err:
    if (platform_data->server_cert) {
        free(platform_data->server_cert);
    }
    if (platform_data->client_key) {
        free(platform_data->client_key);
    }
    if (platform_data->client_cert) {
        free(platform_data->client_cert);
    }
    if (platform_data->mqtt_host) {
        free(platform_data->mqtt_host);
    }
    free(platform_data);
    return ESP_FAIL;
}
