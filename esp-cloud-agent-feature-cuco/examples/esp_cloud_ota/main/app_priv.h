/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#pragma once
#include <esp_err.h>
#include <stdint.h>
#include <esp_cloud_ota.h>
esp_err_t app_ota_perform(esp_cloud_ota_handle_t ota_handle, const char *url, void *priv);
