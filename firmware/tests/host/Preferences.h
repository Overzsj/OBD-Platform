#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <stdint.h>
static std::map<std::string,std::vector<uint8_t>> testNvs;
static bool testWriteFailure=false;
static unsigned testWrites=0;
class Preferences {
public:
    bool begin(const char*,bool){return true;}
    size_t getBytesLength(const char* k){return testNvs[k].size();}
    size_t getBytes(const char* k,void* out,size_t n){if(testNvs[k].size()!=n)return 0;memcpy(out,testNvs[k].data(),n);return n;}
    size_t putBytes(const char* k,const void* in,size_t n){++testWrites;if(testWriteFailure)return 0;auto p=static_cast<const uint8_t*>(in);testNvs[k]=std::vector<uint8_t>(p,p+n);return n;}
    uint32_t getUInt(const char* k,uint32_t d){uint32_t v;return getBytes(k,&v,sizeof(v))?v:d;}
    size_t putUInt(const char* k,uint32_t v){return putBytes(k,&v,sizeof(v));}
    bool getBool(const char* k,bool d){uint8_t v;return getBytes(k,&v,1)?v:d;}
    size_t putBool(const char* k,bool v){uint8_t value=v;return putBytes(k,&value,1);}
};
