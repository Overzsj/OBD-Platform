#pragma once
#include <cstring>
#include <stdint.h>
constexpr int ESP_OK=0,ESP_PARTITION_TYPE_DATA=1,ESP_PARTITION_SUBTYPE_DATA_SPIFFS=2;
struct esp_partition_t {size_t size=1024;};
static bool testPartitionBlank=true;
inline const esp_partition_t* esp_partition_find_first(int,int,const char*){static esp_partition_t p;return &p;}
inline int esp_partition_read(const esp_partition_t*,size_t,void* p,size_t n){memset(p,testPartitionBlank?255:0,n);return 0;}
