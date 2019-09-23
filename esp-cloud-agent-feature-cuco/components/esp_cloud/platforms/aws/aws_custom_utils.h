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

#include "stdint.h"
#include "aws_iot_error.h"
#include "aws_iot_shadow_json_data.h"

IoT_Error_t custom_aws_iot_shadow_add_desired(char *pJsonDocument,
                        size_t maxSizeOfJsonDocument,
                        uint8_t count,
                        jsonStruct_t **handler);
IoT_Error_t custom_aws_iot_shadow_add_reported(char *pJsonDocument,
                        size_t maxSizeOfJsonDocument,
                        uint8_t count,
                        jsonStruct_t **handler);
