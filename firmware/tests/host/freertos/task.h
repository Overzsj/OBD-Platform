#pragma once
#include "FreeRTOS.h"
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,size_t,void*,int,TaskHandle_t*,int){return 0;}
