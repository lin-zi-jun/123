/* OTA Upgrade

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <esp_https_ota.h>
#include "app_priv.h"
#include <esp_cloud_storage.h>
#include <esp_cloud_ota.h>

static const char *TAG = "app_ota";

esp_err_t app_ota_perform(esp_cloud_ota_handle_t ota_handle, const char *url, void *priv)
{
    ESP_LOGI(TAG, "app_ota_perform called");
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
    if (err == ESP_OK) {
        esp_cloud_report_ota_status(ota_handle, OTA_STATUS_SUCCESS, "Finished Successfully");
    } else {
        char info[50];
        snprintf(info, sizeof(info), "esp_https_ota failed with %d: %s", err, esp_err_to_name(err));
        esp_cloud_report_ota_status(ota_handle, OTA_STATUS_FAILED, info);
    }
    return err;
}
