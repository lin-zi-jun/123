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
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sdkconfig.h>
#include <esp_heap_caps.h>

void *esp_cloud_mem_malloc(int size)
{
    void *data;
#if (CONFIG_ESP_CLOUD_USE_SPIRAM_FOR_ALLOCATIONS && (CONFIG_SPIRAM_USE_CAPS_ALLOC || CONFIG_SPIRAM_USE_MALLOC))
    data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    data = malloc(size);
#endif
    return data;
}

void *esp_cloud_mem_calloc(int n, int size)
{
    void *data;
#if (CONFIG_ESP_CLOUD_USE_SPIRAM_FOR_ALLOCATIONS && (CONFIG_SPIRAM_USE_CAPS_ALLOC || CONFIG_SPIRAM_USE_MALLOC))
    data = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    data = calloc(n, size);
#endif
    return data;
}

void *esp_cloud_mem_realloc(void *old_ptr, int old_size, int new_size)
{
    if (new_size <= old_size) {
        return old_ptr;
    }
    void *new_ptr = esp_cloud_mem_calloc(1, new_size);
    if (new_ptr) {
        memcpy(new_ptr, old_ptr, old_size);
        free(old_ptr);
        old_ptr = NULL;
    }
    return new_ptr;
}

void *esp_cloud_mem_alloc_dma(int n, int size)
{
    void *data =  NULL;
    data = heap_caps_malloc(n * size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (data) {
        memset(data, 0, n * size);
    }
    return data;
}

char *esp_cloud_mem_strdup(const char *str)
{
#if (CONFIG_ESP_CLOUD_USE_SPIRAM_FOR_ALLOCATIONS && (CONFIG_SPIRAM_USE_CAPS_ALLOC || CONFIG_SPIRAM_USE_MALLOC))
    char *copy = heap_caps_malloc(strlen(str) + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); //1 extra for the '\0' NULL character
#else
    char *copy = malloc(strlen(str) + 1);
#endif
    if (copy) {
        strcpy(copy, str);
    }
    return copy;
}

void esp_cloud_mem_free(void *ptr)
{
    free(ptr);
}
