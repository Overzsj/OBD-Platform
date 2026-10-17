#pragma once
#include "FreeRTOS.h"
inline SemaphoreHandle_t xSemaphoreCreateBinary(){return nullptr;}
inline int xSemaphoreTake(SemaphoreHandle_t,int){return 0;}
inline int xSemaphoreGive(SemaphoreHandle_t){return 0;}
inline void vSemaphoreDelete(SemaphoreHandle_t){}
