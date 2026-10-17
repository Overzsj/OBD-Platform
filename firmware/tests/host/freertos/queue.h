#pragma once
#include "FreeRTOS.h"
inline QueueHandle_t xQueueCreate(size_t,size_t){return nullptr;}
inline int xQueueReceive(QueueHandle_t,void*,int){return 0;}
inline int xQueueSend(QueueHandle_t,const void*,int){return 0;}
inline void vQueueDelete(QueueHandle_t){}
