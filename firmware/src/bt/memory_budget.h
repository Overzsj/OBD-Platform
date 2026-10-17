#pragma once
#include <stddef.h>
namespace obd {
constexpr size_t kBtFreeReserve=20480,kBtLargestReserve=8192;
inline bool btConnectionMemoryReady(size_t free,size_t largest){return free>=kBtFreeReserve && largest>=kBtLargestReserve;}
}
