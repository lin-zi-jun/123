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

#include <stdio.h>

void *esp_cloud_mem_malloc(int size);
void *esp_cloud_mem_calloc(int n, int size);
void *esp_cloud_mem_realloc(void *old_ptr, int old_size, int new_size);
char *esp_cloud_mem_strdup(const char *str);

void *esp_cloud_mem_alloc_dma(int n, int size);

void esp_cloud_mem_free(void *ptr);
