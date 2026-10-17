#pragma once
#include <string.h>
#include "adapter_catalog.h"
namespace obd {
inline bool adapterNameAllowed(const char* name){
    char upper[96]{};size_t i=0;for(;name[i]&&i+1<sizeof(upper);++i)upper[i]=name[i]>='a'&&name[i]<='z'?name[i]-32:name[i];
    for(auto bad:adapters::discouraged)if(strstr(upper,bad))return false;
    for(auto known:adapters::names)if(strstr(upper,known))return true;
    for(auto& def:adapters::ble){char key[32]{};size_t j=0;for(;def.name[j]&&j+1<sizeof(key);++j)key[j]=def.name[j]>='a'&&def.name[j]<='z'?def.name[j]-32:def.name[j];if(strstr(upper,key))return true;}
    return false;
}
}
