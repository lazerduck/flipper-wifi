#pragma once
#include "FreeRTOS.h"
#include <stdint.h>

typedef BaseType_t (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(void (*task_fn)(void *),
                       const char *name,
                       uint32_t    stack_depth,
                       void       *param,
                       UBaseType_t priority,
                       TaskHandle_t *created_task);

#define vTaskDelete(h) ((void)(h))
