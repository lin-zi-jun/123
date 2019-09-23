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

#include <esp_log.h>
#include <esp_https_ota.h>
#include <esp_cloud_storage.h>
#include <esp_cloud_ota.h>
#include <cloud_agent.h>

#define OTA_CHECK_TIME (10 * 1000)      // 10 seconds

static const char *TAG = "[cloud_agent]";

static esp_cloud_handle_t esp_cloud_handle;
static void (*cloud_agent_cb)(cloud_agent_event_t event) = NULL;
static bool init_done = false;

static void cloud_agent_init_done_cb()
{
    if (cloud_agent_cb) {
        (*cloud_agent_cb)(CLOUD_AGENT_INIT_DONE);
    }
}

static esp_err_t cloud_agent_ota_perform(esp_cloud_ota_handle_t ota_handle, const char *url, void *priv)
{
    if (cloud_agent_cb) {
        (*cloud_agent_cb)(CLOUD_AGENT_OTA_START);
    }

    if (!url) {
        return ESP_FAIL;
    }
    char *ota_server_cert = esp_cloud_storage_get("ota_server_cert");
    if (!ota_server_cert) {
        esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, "Server Certificate Absent");
        return ESP_FAIL;
    }

    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = ota_server_cert,
        .buffer_size = 1024
    };

    esp_err_t err = esp_https_ota(&config);
    free(ota_server_cert);

    if (cloud_agent_cb) {
        (*cloud_agent_cb)(CLOUD_AGENT_OTA_END);
    }

    if (err == ESP_OK) {
        esp_cloud_report_ota_status(ota_handle, OTA_STATUS_SUCCESS, "Finished Successfully");
    } else {
        char info[50];
        snprintf(info, sizeof(info), "esp_https_ota failed with %d: %s", err, esp_err_to_name(err));
        esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, info);
    }
    return err;
}

void cloud_agent_ota_stop()
{
    esp_cloud_stop(esp_cloud_handle);
}

void cloud_agent_ota_start(cloud_agent_device_cfg_t *cloud_agent_device_cfg, void (*cloud_agent_app_cb)(cloud_agent_event_t event))
{
    /* TODO: Currently we are sending the CLOUD_AGENT_INIT_DONE event only once. But infact we should send that (or some other event) everytime this API is called. */
    if (init_done == false) {
        cloud_agent_cb = cloud_agent_app_cb;
        esp_cloud_config_t cloud_cfg = {
            .id = {
                .name = cloud_agent_device_cfg->name,
                .type = cloud_agent_device_cfg->type,
                .model = cloud_agent_device_cfg->model,
                .fw_version = cloud_agent_device_cfg->fw_version,
            },
            .enable_time_sync = true,
            .reconnect_attempts = 5,
        };

        esp_cloud_init(&cloud_cfg, &esp_cloud_handle);
        esp_cloud_queue_work(esp_cloud_handle, cloud_agent_init_done_cb, NULL);
        esp_cloud_enable_ota(esp_cloud_handle, cloud_agent_ota_perform, NULL);
        init_done = true;
    }
    esp_cloud_start(esp_cloud_handle);
}
