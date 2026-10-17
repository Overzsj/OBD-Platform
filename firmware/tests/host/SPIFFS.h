#pragma once
#include "Arduino.h"
#include <map>
#include <vector>
#include <string>
constexpr const char* FILE_READ="r";constexpr const char* FILE_APPEND="a";
static std::map<std::string,std::vector<uint8_t>> testFiles;
static bool testFsMountable=true;static unsigned testFormats=0;static size_t testWriteLimit=~size_t(0);
static bool testBufferedWriteLost=false;
class File {
public:
    File()=default;File(std::string name,bool valid,bool append):name_(name),valid_(valid),pos_(append?testFiles[name].size():0){}
    explicit operator bool()const{return valid_;}
    size_t size()const{return valid_?testFiles[name_].size():0;}
    size_t position()const{return pos_;}
    size_t read(uint8_t* p,size_t n){if(!valid_)return 0;auto& data=testFiles[name_];n=min(n,data.size()-min(pos_,data.size()));memcpy(p,data.data()+pos_,n);pos_+=n;return n;}
    size_t write(const uint8_t* p,size_t n){if(!valid_)return 0;n=min(n,testWriteLimit);if(testBufferedWriteLost)return n;auto& data=testFiles[name_];if(pos_+n>data.size())data.resize(pos_+n);memcpy(data.data()+pos_,p,n);pos_+=n;return n;}
    bool seek(size_t pos){if(pos>size())return false;pos_=pos;return true;}
    void close(){valid_=false;}
private:std::string name_;bool valid_=false;size_t pos_=0;
};
class TestSpiffs {
public:
    bool begin(bool,const char* ="/spiffs",uint8_t=10){return testFsMountable;}
    bool format(){++testFormats;testFiles.clear();testFsMountable=true;return true;}
    File open(const char* name,const char* mode){bool append=!strcmp(mode,"a");return File(name,append||testFiles.count(name),append);}
    bool exists(const char* name){return testFiles.count(name);}
    bool remove(const char* name){return testFiles.erase(name);}
};
static TestSpiffs SPIFFS;
