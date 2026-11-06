#pragma once
#include "FreeRTOS.h"
struct tskTaskControlBlock;
typedef struct tskTaskControlBlock* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth,
                                   void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pvCreatedTask,
                                   const BaseType_t xCoreID);
void vTaskDelay(const TickType_t xTicksToDelay);
