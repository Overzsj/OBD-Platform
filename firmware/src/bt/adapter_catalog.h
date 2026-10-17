#pragma once
#include <stddef.h>
namespace obd { namespace adapters {
static const char* const names[]={"ELM327","OBDII","OBD2","OBD II","VLINKER","VGATE","VEEPEAK","VIECAR"};
static const char* const discouraged[]={"MICRO MECHANIC","SCAN TOOL PRO","THINMI.COM"};
struct BleDefinition {const char* name;const char* service;const char* tx;const char* rx;};
// Common FFF0 UART example; confirm against the adapter vendor before use.
static const BleDefinition ble[]={{"OBD BLE","0000fff0-0000-1000-8000-00805f9b34fb","0000fff2-0000-1000-8000-00805f9b34fb","0000fff1-0000-1000-8000-00805f9b34fb"}};
constexpr const char* wifiHost="192.168.0.10";
constexpr unsigned wifiPort=35000;
}}
