#pragma once
#include <stdint.h>
#include <stddef.h>
constexpr int VSPI=3,MSBFIRST=1;
static uint16_t testPanel[480*320]{};
class SPISettings {public:SPISettings(uint32_t,int,int){}};
// Decode the actual ILI9488 address windows and RGB666 stream into a host panel.
class SPIClass {
public:
    explicit SPIClass(int){}
    void begin(int,int,int,int){}
    void beginTransaction(const SPISettings&){}
    void endTransaction(){}
    void write(uint8_t command){cmd_=command;used_=0;if(command==0x2c){x_=left_;y_=top_;}}
    void writeBytes(const uint8_t* bytes,size_t n) {
        for(size_t i=0;i<n;++i) {
            if(cmd_==0x2a || cmd_==0x2b) {
                temp_[used_++]=bytes[i];
                if(used_==4){if(cmd_==0x2a){left_=(temp_[0]<<8)|temp_[1];right_=(temp_[2]<<8)|temp_[3];}else top_=(temp_[0]<<8)|temp_[1];used_=0;}
            } else if(cmd_==0x2c) {
                temp_[used_++]=bytes[i];
                if(used_==3){if(x_<480 && y_<320)testPanel[y_*480+x_]=((temp_[0]>>3)<<11)|((temp_[1]>>2)<<5)|(temp_[2]>>3);used_=0;if(++x_>right_){x_=left_;++y_;}}
            }
        }
    }
private:uint8_t cmd_=0,temp_[4]{},used_=0;uint16_t left_=0,right_=479,top_=0,x_=0,y_=0;
};
