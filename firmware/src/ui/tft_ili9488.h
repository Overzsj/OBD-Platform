#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "ili9488_config.h"

#pragma GCC push_options
#pragma GCC optimize ("O3")
namespace obd {
// initialized() reports commands sent; it cannot detect a write-only panel.
// RGB565 pixels in the compositor are converted to three-byte RGB666 on SPI.
class Ili9488 {
public:
    static constexpr uint16_t kWidth=480,kHeight=320;
    static constexpr size_t kWirePixels=512;
    void begin() {
        pinMode(TFT_PIN_CS,OUTPUT); digitalWrite(TFT_PIN_CS,HIGH);
        pinMode(TFT_PIN_DC,OUTPUT); digitalWrite(TFT_PIN_DC,HIGH);
        if (TFT_PIN_BL>=0) { pinMode(TFT_PIN_BL,OUTPUT); digitalWrite(TFT_PIN_BL,LOW); }
        spi_.begin(TFT_PIN_SCLK,-1,TFT_PIN_MOSI,TFT_PIN_CS);
        if (TFT_PIN_RST>=0) {
            pinMode(TFT_PIN_RST,OUTPUT); digitalWrite(TFT_PIN_RST,HIGH); delay(10);
            digitalWrite(TFT_PIN_RST,LOW); delay(20);
            digitalWrite(TFT_PIN_RST,HIGH); delay(120);
        }
        command(0x01); delay(150);
        // See docs/驱动依据.md for ILI9488 command definitions and references.
        const uint8_t positive[]={0x00,0x03,0x09,0x08,0x16,0x0A,0x3F,0x78,0x4C,0x09,0x0A,0x08,0x16,0x1A,0x0F};
        const uint8_t negative[]={0x00,0x16,0x19,0x03,0x0F,0x05,0x32,0x45,0x46,0x04,0x0E,0x0D,0x35,0x37,0x0F};
        command(0xE0,positive,sizeof(positive)); command(0xE1,negative,sizeof(negative));
        const uint8_t power1[]={0x17,0x15},power2[]={0x41},vcom[]={0x00,0x12,0x80};
        command(0xC0,power1,2); command(0xC1,power2,1); command(0xC5,vcom,3);
        const uint8_t madctl=(TFT_ROTATION==1?0x20:0xE0)|(TFT_COLOR_BGR?0x08:0);
        const uint8_t format=0x66;
        command(0x36,&madctl,1); command(0x3A,&format,1);
        const uint8_t interfaceMode[]={0x00},frameRate[]={0xA0},inversion[]={0x02};
        const uint8_t displayFunction[]={0x02,0x02,0x3B},entryMode[]={0xC6},adjust[]={0xA9,0x51,0x2C,0x82};
        command(0xB0,interfaceMode,1); command(0xB1,frameRate,1); command(0xB4,inversion,1);
        command(0xB6,displayFunction,3); command(0xB7,entryMode,1); command(0xF7,adjust,4);
        command(TFT_PANEL_INVERT?0x21:0x20);
        command(0x11); delay(120); command(0x29); delay(25);
        initialized_=true;
        if (TFT_PIN_BL>=0) digitalWrite(TFT_PIN_BL,HIGH);
    }
    bool initialized() const { return initialized_; }
    uint32_t bytesWritten() const { return bytesWritten_; }
    uint32_t blocksWritten() const { return blocksWritten_; }
    static constexpr uint8_t expand5(uint8_t v) { return uint8_t(((v<<1)|(v>>4))<<2); }
    static constexpr uint32_t wireColor(uint16_t p) {
        return (uint32_t(expand5((p>>11)&31))<<16) | (uint32_t((p>>5)&63)<<10) | expand5(p&31);
    }
    static void rgb666(uint16_t p,uint8_t* out) {
        const uint32_t color=wireColor(p);
        out[0]=color>>16; out[1]=color>>8; out[2]=color;
    }
    // Input starts at rectangle top-left; stride is in RGB565 words.
    void writeRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t* pixels,uint16_t stride) {
        if (!initialized_ || !pixels || x>=kWidth || y>=kHeight || !w || !h || stride<w) return;
        if (w>kWidth-x) w=kWidth-x;
        if (h>kHeight-y) h=kHeight-y;
        const uint16_t x1=x+w-1,y1=y+h-1;
        uint8_t cols[]={uint8_t(x>>8),uint8_t(x),uint8_t(x1>>8),uint8_t(x1)};
        uint8_t rows[]={uint8_t(y>>8),uint8_t(y),uint8_t(y1>>8),uint8_t(y1)};
        select(); send(0x2A,cols,4); send(0x2B,rows,4); send(0x2C,nullptr,0);
        size_t used=0;
        for (uint16_t row=0;row<h;++row) for (uint16_t col=0;col<w;++col) {
            rgb666(pixels[size_t(row)*stride+col],wire_+used*3);
            if (++used==kWirePixels) { spi_.writeBytes(wire_,used*3); used=0; }
        }
        if (used) spi_.writeBytes(wire_,used*3);
        deselect(); bytesWritten_+=uint32_t(w)*h*3; ++blocksWritten_;
    }
    void writeRows(uint16_t row,uint16_t count,const uint16_t* pixels) { writeRect(0,row,kWidth,count,pixels,kWidth); }
private:
    void select() { spi_.beginTransaction(SPISettings(TFT_SPI_HZ,MSBFIRST,TFT_SPI_MODE)); digitalWrite(TFT_PIN_CS,LOW); }
    void deselect() { digitalWrite(TFT_PIN_CS,HIGH); spi_.endTransaction(); }
    void send(uint8_t cmd,const uint8_t* data,size_t len) {
        digitalWrite(TFT_PIN_DC,LOW); spi_.write(cmd); digitalWrite(TFT_PIN_DC,HIGH);
        if (len) spi_.writeBytes(data,len);
    }
    void command(uint8_t cmd,const uint8_t* data=nullptr,size_t len=0) { select(); send(cmd,data,len); deselect(); }
    SPIClass spi_{VSPI};
    uint8_t wire_[kWirePixels*3]{};
    uint32_t bytesWritten_=0,blocksWritten_=0;
    bool initialized_=false;
};
static_assert(Ili9488::wireColor(0xF800)==0xFC0000,"RGB666 red wire vector");
static_assert(Ili9488::wireColor(0x07E0)==0x00FC00,"RGB666 green wire vector");
static_assert(Ili9488::wireColor(0x001F)==0x0000FC,"RGB666 blue wire vector");
static_assert(Ili9488::wireColor(0xFFFF)==0xFCFCFC,"RGB666 white wire vector");
static_assert(Ili9488::wireColor(0x0000)==0x000000,"RGB666 black wire vector");
static_assert((Ili9488::wireColor(0x8410)&0x030303)==0,"RGB666 padding bits are zero");
} // namespace obd
#pragma GCC pop_options
