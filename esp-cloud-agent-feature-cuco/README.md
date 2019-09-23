# ESP Cloud Agent

## Repository Structure

The ESP Cloud Agent repository has 2 main folders

- **components/** : Contains primarily the ESP Cloud Agent component and a few other components required for the ESP Cloud and examples
- **examples/** : Examples to demonstrate the ESP Cloud Agent features

### ESP Cloud Component
The ESP Cloud component (components/esp_cloud) is the main component for the ESP Cloud Agent. It is structured as below:

- **include** : The header files for the ESP Cloud which can be used directly by the applications
- **src** : Source files for the ESP Cloud
- **platform** : Platform specific code and header files internally used by the ESP Cloud. Currently, only the AWS IoT platform is supported. Others will be added later
- **utils** : Various ESP Cloud related utilities that can be used by both, the ESP Cloud as well as applications. Following utilities are currently available:
	- OTA : For OTA Upgrades as per the specifications defined for ESP Cloud
	- Diagnostics: For reporting optional diagnostics information to the ESP Cloud
	- Storage: For reading cloud credentials from the read-only factory NVS partition

	
## Key Concepts

### Cloud Configuration
> Ref. components/esp\_cloud/include/esp\_cloud.h

This is specified by the `esp_cloud_config_t` structure. As the name suggests, it specifies the configuration to be used fo initialising the ESP Cloud agent. This currently has information like the identifier, static/dynamic parameter count, reconnect attempts, etc.

### Cloud Identifier
> Ref. components/esp\_cloud/include/esp\_cloud.h

This is specified by the `esp_cloud_identifier_t` structure. It provides the mandatory identification information like name, model, type and fw_version. This is used to populate the database for the device in the ESP Cloud

### Cloud Handle
This is an opaque handle initialised by `esp_cloud_init()` and which is required for using all the other ESP Cloud APIs.

### Static Parameters
> Ref. components/esp\_cloud/include/esp\_cloud.h

Static parameters are the additional variables to be reported to the ESP Cloud along-with the other identification information. This reporting is done only once per boot cycle after a successful connection is established with the ESP Cloud. Eg. serial_number

```
Usage:
esp_cloud_add_static_string_param(g_esp_cloud_handle, "serial_number", "012345");
```

### Dynamic Parameters
> Ref. components/esp\_cloud/include/esp\_cloud.h

Dynamic parameters indicate the current state of the device. These can be something like the on/off status of a switch, brightness of a lightbulb, temperature reported by a sensor, etc. These can change locally and some can also be changed from the ESP Cloud.

```
Usage:
/* Read-Write. Can be changed from ESP Cloud  */
esp_cloud_add_dynamic_bool_param(g_esp_cloud_handle, "output", false, output_callback, my_priv_data);
/* Read-Only. Has a NULL callback. Can change only locally */
esp_cloud_add_dynamic_bool_param(g_esp_cloud_handle, "temperature", false, NULL, NULL);
```

### OTA
> Ref. components/esp\_cloud/utils/include/esp\_cloud\_ota.h

Managing OTA Upgrades is one of the primary features of the ESP Cloud. Please refer the ESP Cloud Dashboard Manual for information on how to trigger an OTA. On the device side, a simple callback needs to be registered to manage the OTA. The callback should fetch the OTA image, perform the upgrade and report the status to ESP Cloud.

```
Usage:
esp_err_t app_ota_perform(esp_cloud_ota_handle_t ota_handle, const char *url, void *priv)
{
	...
	esp_cloud_report_ota_status(ota_handle, OTA_STATUS_IN_PROGRESS, "<additional info>");
	...
	...
	esp_cloud_report_ota_status(ota_handle, OTA_STATUS_SUCCESS, "<additional success info>");
	return ESP_OK;
}
{
	...
	esp_cloud_enable_ota(esp_cloud_handle, app_ota_perform, NULL);
	...
}
```

#### Storage
> Ref. components/esp\_cloud/utils/include/esp\_cloud\_storage.h

The ESP Cloud storage will be required by the application only to fetch the OTA Server Certificate to be used in the OTA callback.

```
Usage:
{
	...
	char *ota_server_cert = esp_cloud_storage_get("ota_server_cert");
	...
	free(ota_server_cert);
	...
}
```


### Diagnostics
> Ref. components/esp\_cloud/utils/include/esp\_cloud\_diagnostics.h

The diagnostics module allows applications to report any type of diagnostics data to the ESP Cloud. There are 2 ways of reporting this data

1. **One shot**: The `esp_cloud_diagnostics_add_data()` API can be used to add diagnostics data which will be reported to ESP Cloud by the agent as soon as it gets some idle time.

2. **Periodic**: Periodic diagnostics handlers can be registered using the `esp_cloud_diagnostics_register_periodic_handler()` API. The handler will be called once after ESP Cloud connects successfully and then periodically thereafter as per the period defined. The `esp_cloud_diagnostics_send_data()` API needs to be used from the handler to actually send the data to ESP Cloud. Note that the handler will get called as per the exact period specified, but the actual reporting period can be skewed because of connectivity issues or other tasks being performed by the ESP Cloud agent.

## Application Code Structure
The ESP Cloud specific application code is divided into 3 parts:

1. Initialise
2. Configure features
3. Start

```
/* Initialise */
 esp_cloud_config_t cloud_cfg = {
        .id = {
            .name = "ESP Outlet",
            .type = "Outlet",
            .model = "ESP-Wall-Outlet-1",
            .fw_version = FW_VERSION,
        },
        .enable_time_sync = true,
        .reconnect_attempts = 5,
        .static_cloud_params_count = 1,
        .dynamic_cloud_params_count = 1,
    };
    esp_cloud_init(&cloud_cfg, &g_esp_cloud_handle);

/* Configure Features */
    esp_cloud_add_static_string_param(g_esp_cloud_handle, "serial_number", "012345");
    esp_cloud_add_dynamic_bool_param(g_esp_cloud_handle, "output", false, output_callback, my_priv_data);
    esp_cloud_enable_ota(g_esp_cloud_handle, app_ota_perform, NULL);
    esp_cloud_diagnostics_register_periodic_handler(g_esp_cloud_handle, diag_handler, 5 * 60, data_5min);
    
/* Start (Preferably after a successful Wi-Fi connection, to avoid unnecessary connection failures */
    esp_cloud_start(g_esp_cloud_handle);
```

## Examples
The ESP Cloud Agent repository provides 2 example applications

1. **examples/esp\_cloud\_ota**: Simple example to demonstrate the OTA. Check the OTA callback in `main/app_ota.c`
2. **examples/esp\_cloud\_control**: This is a slightly advanced example which demonstrates the following
	- OTA: Check the OTA callback in `main/app_ota.c`.
	- Control: Check the handling of the "output" parameter in `main/app_ota.c` and `main/app_driver.c`
	- Diagnostics: One time diagnostics (reboot reason) and 2 dummy periodic diagnostics handlers. Check the code in `main/app_ota.c`

### Compilation
If you don't have IDF set up already, follow the instructions from [ESP IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) and set up ESP IDF, Toolchain and the Serial Console. Follow these steps after you connect the board:

```
~# cd /path/to/esp-idf
~# git checkout -b release/v3.3 remotes/origin/release/v3.3
~# cd /path/to/esp-cloud-agent/examples/esp_cloud_ota/
~# export IDF_PATH=/path/to/esp-idf
~# export ESPPORT=/dev/<board_identifier>
~# export ESPBAUD=2000000
~# make defconfig
~# make menuconfig (set Example Configuration -> Wi-Fi SSID, Passphrase)
~# make erase_flash
~# make -j8
```

Make a note of the python command printed at the end of "make -j8". This will be required later.

### Programming Cloud Credentials
Copy all the files from the zip file you downloaded from the dashboard into the `esp_cloud_ota/main/cloud_cfg/` folder. Create mfg.bin as below.

```
~# cd /path/to/esp-cloud-agent/examples/esp_cloud_ota
~# python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py --input mfg_config.csv --output mfg.bin --size 0x6000
```

Flash all the components using the python command from the Compilation step above, with additional field "0x340000 mfg.bin" appended and monitor the logs

```
~# python /path/to/idf/esp-idf/components/esptool_py/esptool/esptool.py --chip esp32 --port /dev/tty.usbserial-145101 --baud 2000000 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0xf000 /path/to/esp_cloud_ota/build/ota_data_initial.bin 0x1000 /path/to/esp_cloud_ota/build/bootloader/bootloader.bin 0x20000 /path/to/esp_cloud_ota/build/esp_cloud_ota.bin 0x8000 /path/to/esp_cloud_ota/build/partitions.bin 0x340000 mfg.bin
```

### Monitoring the Device
Just execute the below to monitor the serial console of the device

```
~# make monitor
```

### Remote Control
Remote control via the dashboard or a phone app is not yet supported, but will be available soon. For the time being, curl can be used for remote control using the device credentials.

To test this, follow all the steps as above, but use the example `esp_cloud_control`. This example registers a dynamic boolean parameter named “output”. The make monitor console will show a line as below when the device connects to the ESP Cloud

```
aws_cloud: Update Shadow: {"state":{"reported":{"output":false,"fw_version":"1.0"}}, “clientToken":"f639bef3-d83e-4d88-83e6-3e96bf69378f-0"}
```

To change the output to true, from the terminal of your host machine, execute the following:

```
curl -d ‘{"state":{"desired":{"output":true}}}' --tlsv1.2 --cert </path/to/device.crt> --key </path/to/device.key> <contents of endpoint.info>/things/<contents of device.info>/shadow | python -mjson.tool
```

> Note that the same credentials use for the device are being used here.
