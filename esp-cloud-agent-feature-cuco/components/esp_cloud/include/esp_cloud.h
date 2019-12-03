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
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#define MAX_VERSION_STRING_LEN  16

/** Cloud identifier, mandated by ESP Cloud */
typedef struct {
    char *name;
    char *type;
    char *model;
    char *fw_version;
} esp_cloud_identifier_t;

extern char *ota_vertion;

/** Cloud configuration required during initialization */
typedef struct {
    esp_cloud_identifier_t id;
    /* Setting this true will enable SNTP and fetch the current time before
     * attempting to connect to the ESP Cloud service
     */
    bool enable_time_sync;
    /* Maximum number of static parameters that the application wants to add.
     * Must be set to zero if there are no static parameters.
     */
    uint8_t static_cloud_params_count;
    /* Maximum number of static parameters that the application wants to add.
     * Must be set to zero if there are no dynamic parameters.
     */
    uint8_t dynamic_cloud_params_count;
    /* Maximum number of times the device will attempt to connect to the
     * ESP Cloud
     */
    uint16_t reconnect_attempts;
} esp_cloud_config_t;

/** ESP Cloud Parameter Value type */
typedef enum {
    /** Invalid */
    CLOUD_PARAM_TYPE_INVALID = 0,
    /** Boolean */
    CLOUD_PARAM_TYPE_BOOLEAN,
    /** Integer. Mapped to a 32 bit signed integer */
    CLOUD_PARAM_TYPE_INTEGER,
    /** Floating point number */
    CLOUD_PARAM_TYPE_FLOAT,
    /** NULL terminated string */
    CLOUD_PARAM_TYPE_STRING,
} esp_cloud_param_val_type_t;

/** ESP Cloud Parameter Value. Used for both, static and dynamic characteristics */
typedef struct {
    /** Type of Value */
    esp_cloud_param_val_type_t type;
    /** Actual Value. Depends on the type */
    union {
        bool b;
        int i;
        float f;
        char *s;
    } val;
    /** Size of value. Valid only for string, to indicate the maximum size required to
     * hold the string, in case of a dynamic parameter
     */
    size_t val_size;
} esp_cloud_param_val_t;

/** Cloud handle to be used for all ESP Cloud APIs */
typedef void * esp_cloud_handle_t;

/** Initialize ESP Cloud Agent
 *
 * This initializes the internal data required by ESP Cloud agent and allocates memory as required.
 * This should be the first call before using any other ESP Cloud API
 *
 * @param[in] config Configuration to be used by the ESP Cloud.
 * @param[out] Pointer to a handle that will be initialized by this function.
 *
 * @return ESP_OK on success.
 * @return error in case of failures.
 */
esp_err_t esp_cloud_init(esp_cloud_config_t *config, esp_cloud_handle_t *handle);

/** Start ESP Cloud Agent
 *
 * This call starts the actual ESP Cloud thread. This should preferably be called after a
 * successful Wi-Fi connection in order to avoid unnecessary failures.
 *
 * @param[in] handle The ESP Cloud Handle
 *
 * @return ESP_OK on success.
 * @return error in case of failures.
 */
esp_err_t esp_cloud_start(esp_cloud_handle_t handle);

/** Stop ESP Cloud Agent
 *
 * This call stops the ESP Cloud Agent instance started earlier by esp_cloud_start().
 *
 * @param[in] handle The ESP Cloud Handle
 *
 * @return ESP_OK on success.
 * @return error in case of failures.
 */
esp_err_t esp_cloud_stop(esp_cloud_handle_t handle);

/** Get ESP Cloud Handle
 *
 * Gets handle to an initialised ESP Cloud instance
 *
 * @return ESP Cloud Handle
 */
esp_cloud_handle_t esp_cloud_get_handle();

/** Get Device Id
 * 
 * Returns pointer to the NULL terminated ESP Cloud ID string
 *
 * @param[in] handle The ESP Cloud Handle
 *
 * @return Pointer to a NULL terminated ESP Cloud ID string.
 */
char *esp_cloud_get_device_id(esp_cloud_handle_t handle);

/** Callback for dynamic parameter value changes
 *
 * @param[in] name Name of the parameters used while creating it
 * @param[in] param Pointer to \ref esp_cloud_param_val_t. Use appropriate elements as per the value type
 * @param[in] priv_data Pointer to the private data passed while creating the dynamic parameter.
 *
 * @return ESP_OK on success. The agent will report the changed value to ESP Cloud in this case
 * @return error in case of any error. No value will be reported back to the ESP Cloud in this case
 */
typedef esp_err_t (*esp_cloud_param_callback_t)(const char *name, esp_cloud_param_val_t *param, void *priv_data);

/** Add a Static Boolean parameter
 *
 * @note Static parameters are reported only once after a boot-up along with
 * the other parameters of esp_cloud_identifier_t.
 * Eg. Serial Number
 * 
 * @param[in] handle The ESP Cloud Handle
 * @param[in] name Name of the parameter
 * @param[in] val Value of the parameter
 *
 * @return ESP_OK if the parameter was added successfully.
 * @return error in case of failures. Check the static_cloud_params_count in esp_cloud_config_t once.
 */
esp_err_t esp_cloud_add_static_bool_param(esp_cloud_handle_t handle, const char *name, bool val);

/** Add a Static Integer parameter
 *
 * @note Static parameters are reported only once after a boot-up along with
 * the other parameters of esp_cloud_identifier_t.
 * Eg. Serial Number
 * 
 * @param[in] handle The ESP Cloud Handle
 * @param[in] name Name of the parameter
 * @param[in] val Value of the parameter
 *
 * @return ESP_OK if the parameter was added successfully.
 * @return error in case of failures. Check the static_cloud_params_count in esp_cloud_config_t once.
 */
esp_err_t esp_cloud_add_static_int_param(esp_cloud_handle_t handle, const char *name, int val);

/** Add a Static Float parameter
 *
 * @note Static parameters are reported only once after a boot-up along with
 * the other parameters of esp_cloud_identifier_t.
 * Eg. Serial Number
 * 
 * @param[in] handle The ESP Cloud Handle
 * @param[in] name Name of the parameter
 * @param[in] val Value of the parameter
 *
 * @return ESP_OK if the parameter was added successfully.
 * @return error in case of failures. Check the static_cloud_params_count in esp_cloud_config_t once.
 */
esp_err_t esp_cloud_add_static_float_param(esp_cloud_handle_t handle, const char *name, float val);

/** Add a Static String parameter
 *
 * @note Static parameters are reported only once after a boot-up along with
 * the other parameters of esp_cloud_identifier_t.
 * Eg. Serial Number
 * 
 * @param[in] handle The ESP Cloud Handle
 * @param[in] name Name of the parameter
 * @param[in] val Pointer to a null terminated string value of the parameter. The ESP Cloud agent
 * will internally make a copy of this.
 * @return ESP_OK if the parameter was added successfully.
 * @return error in case of failures. Check the static_cloud_params_count in esp_cloud_config_t once.
 */
esp_err_t esp_cloud_add_static_string_param(esp_cloud_handle_t handle, const char *name, const char *val);

/** Add a Dynamic Boolean parameter
 *
 * @note Dynamic parameters can change any time and can also be requested to be changed from the Cloud.
 * Eg. Temperature, Outlet state, Lightbulb brightness, etc.
 *
 * Any local changes should be reported to cloud using the esp_cloud_update_bool_param() API.
 * Any remote changes will be reported to the application via the callback, if registered.
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] name Name of the parameter
 * @param[in] val Value of the parameter
 * @param[in] cb (Optional) Callback to be called if a change is requested from cloud. Can be left NULL
 * for parameters like sensor readings, which cannot be changed from cloud.
 * @param[in] priv_data (Optional) Private data that will be passed to the callback. This should stay
 * allocated throughout the lifetime of the parameter
 *
 * @return ESP_OK if the parameter was added successfully.
 * @return error in case of failures. Check the dynamic_cloud_params_count in esp_cloud_config_t once.
 */
esp_err_t esp_cloud_add_dynamic_bool_param(esp_cloud_handle_t handle, const char *name,
        bool val, esp_cloud_param_callback_t cb, void *priv_data);

/** Add a Dynamic Integer parameter
 *
 * @note Dynamic parameters can change any time and can also be requested to be changed from the Cloud.
 * Eg. Temperature, Outlet state, Lightbulb brightness, etc.
 *
 * Any local changes should be reported to cloud using the esp_cloud_update_int_param() API.
 * Any remote changes will be reported to the application via the callback, if registered.
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] name Name of the parameter
 * @param[in] val Value of the parameter
 * @param[in] cb (Optional) Callback to be called if a change is requested from cloud. Can be left NULL
 * for parameters like sensor readings, which cannot be changed from cloud.
 * @param[in] priv_data (Optional) Private data that will be passed to the callback. This should stay
 * allocated throughout the lifetime of the parameter
 *
 * @return ESP_OK if the parameter was added successfully.
 * @return error in case of failures. Check the dynamic_cloud_params_count in esp_cloud_config_t once.
 */
esp_err_t esp_cloud_add_dynamic_int_param(esp_cloud_handle_t handle, const char *name,
        int val, esp_cloud_param_callback_t cb, void *priv_data);

/** Add a Dynamic Float parameter
 *
 * @note Dynamic parameters can change any time and can also be requested to be changed from the Cloud.
 * Eg. Temperature, Outlet state, Lightbulb brightness, etc.
 *
 * Any local changes should be reported to cloud using the esp_cloud_update_float_param() API.
 * Any remote changes will be reported to the application via the callback, if registered.
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] name Name of the parameter
 * @param[in] val Value of the parameter
 * @param[in] cb (Optional) Callback to be called if a change is requested from cloud. Can be left NULL
 * for parameters like sensor readings, which cannot be changed from cloud.
 * @param[in] priv_data (Optional) Private data that will be passed to the callback. This should stay
 * allocated throughout the lifetime of the parameter
 *
 * @return ESP_OK if the parameter was added successfully.
 * @return error in case of failures. Check the dynamic_cloud_params_count in esp_cloud_config_t once.
 */
esp_err_t esp_cloud_add_dynamic_float_param(esp_cloud_handle_t handle, const char *name,
        float val, esp_cloud_param_callback_t cb, void *priv_data);

/** Add a Dynamic String parameter
 *
 * @note Dynamic parameters can change any time and can also be requested to be changed from the Cloud.
 * Eg. Temperature, Outlet state, Lightbulb brightness, etc.
 *
 * Any local changes should be reported to cloud using the esp_cloud_update_string_param() API.
 * Any remote changes will be reported to the application via the callback, if registered.
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] name Name of the parameter
 * @param[in] val Pointer to a null terminated string Value of the parameter. The ESP Cloud agent
 * will internally make a copy of this.
 * @param[in] val_size Maximum expected size (including null terminating byte) required to hold the
 * string throughout its lifetime.
 * @param[in] cb (Optional) Callback to be called if a change is requested from cloud. Can be left NULL
 * for parameters like sensor readings, which cannot be changed from cloud.
 * @param[in] priv_data (Optional) Private data that will be passed to the callback. This should stay
 * allocated throughout the lifetime of the parameter
 *
 * @return ESP_OK if the parameter was added successfully.
 * @return error in case of failures. Check the dynamic_cloud_params_count in esp_cloud_config_t once.
 */
esp_err_t esp_cloud_add_dynamic_string_param(esp_cloud_handle_t handle, const char *name,
        const char *val, size_t val_size, esp_cloud_param_callback_t cb, void *priv_data);

/** Update a Boolean parameter
 *
 * Calling this API will update a dynamic boolean parameter and report it to cloud. This should be
 * used whenever there is any local change.
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] name Name of the parameter
 * @param[in] val New value of the parameter
 *
 * @return ESP_OK if the parameter was updated successfully.
 * @return error in case of failures.
 */
esp_err_t esp_cloud_update_bool_param(esp_cloud_handle_t handle, const char *name, bool val);

/** Update an Integer parameter
 *
 * Calling this API will update a dynamic integer parameter and report it to cloud. This should be
 * used whenever there is any local change.
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] name Name of the parameter
 * @param[in] val New value of the parameter
 *
 * @return ESP_OK if the parameter was updated successfully.
 * @return error in case of failures.
 */
esp_err_t esp_cloud_update_int_param(esp_cloud_handle_t handle, const char *name, int val);

/** Update a Float parameter
 *
 * Calling this API will update a dynamic float parameter and report it to cloud. This should be
 * used whenever there is any local change.
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] name Name of the parameter
 * @param[in] val New value of the parameter
 *
 * @return ESP_OK if the parameter was updated successfully.
 * @return error in case of failures.
 */
esp_err_t esp_cloud_update_float_param(esp_cloud_handle_t handle, const char *name, float val);

/** Update a String parameter
 *
 * Calling this API will update a dynamic string parameter and report it to cloud. This should be
 * used whenever there is any local change.
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] name Name of the parameter
 * @param[in] val New null terminated string value of the parameter
 *
 * @return ESP_OK if the parameter was updated successfully.
 * @return error in case of failures.
 */
esp_err_t esp_cloud_update_string_param(esp_cloud_handle_t handle, const char *name, char *val);

/** Prototype for ESP Cloud Work Queue Function
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] priv_data The private data associated with the work function
 */
typedef void (*esp_cloud_work_fn_t)(esp_cloud_handle_t handle, void *priv_data);

/** Queue execution of a function in ESP Cloud's context
 *
 * This API queues a work function for execution in the ESP Cloud Task's context.
 *
 * @param[in] handle The ESP Cloud Handle
 * @param[in] work_fn The Work function to be queued
 * @param[in] priv_data Private data to be passed to the work function
 *
 * @return ESP_OK on success.
 * @return error in case of failures.
 */
esp_err_t esp_cloud_queue_work(esp_cloud_handle_t handle, esp_cloud_work_fn_t work_fn, void *priv_data);

void ota_report_progress_val_to_app(int progress_val);


enum{
    OTA_FAIL_1,
    OTA_FAIT_2,
    OTA_FINISH_1,
    OTA_FINISH_2,
    OTA_STATUA_MAX
};

void ota_report_msg_status_val_to_app(int result);
