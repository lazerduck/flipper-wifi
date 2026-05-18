#pragma once

/*
 * Minimal FreeRTOS type stubs for host-native unit tests.
 * This file is placed first in the include path so it shadows the real
 * ESP-IDF freertos/FreeRTOS.h.  Only the types used by types.h are defined.
 */

typedef void *        TaskHandle_t;
typedef int           BaseType_t;
typedef unsigned int  UBaseType_t;
typedef unsigned int  TickType_t;

#define pdTRUE  1
#define pdFALSE 0

#define portMAX_DELAY ((TickType_t)0xffffffffU)
