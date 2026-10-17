#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string>
#include <algorithm>
#include <cstring>
using std::min;
using std::max;
using String=std::string;
#ifndef PROGMEM
#define PROGMEM
#endif
inline uint8_t pgm_read_byte(const void* p) {return *static_cast<const uint8_t*>(p);}
inline uint16_t pgm_read_word(const void* p) {return *static_cast<const uint16_t*>(p);}
inline void* memcpy_P(void* out,const void* in,size_t n) {return memcpy(out,in,n);}
extern uint32_t testMillis;
inline uint32_t millis() { return testMillis; }
inline uint32_t micros() {return testMillis*1000;}
inline void delay(uint32_t ms) {testMillis+=ms;}
inline void yield() {}
inline void pinMode(int,int) {}
inline void digitalWrite(int,int) {}
constexpr int OUTPUT=1,HIGH=1,LOW=0;
struct TestEsp {uint32_t getFreeHeap() const {return 45000;}};
static TestEsp ESP;
struct TestSerial { template<typename... T> void printf(const char*,T...) {} };
static TestSerial Serial;
