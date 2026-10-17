#pragma once
#include <Arduino.h>
#include "zh_font_generated.h"
namespace obd {
enum class ZhSize:uint8_t { Small=0,Body=1,Title=2 };
static uint32_t zhMissingGlyphs=0;
inline uint32_t zhNext(const char*& p) {
    const uint8_t first=uint8_t(*p++);if(first<128)return first;
    uint8_t count;uint32_t code,minCode;
    if((first&0xe0)==0xc0){count=1;code=first&31;minCode=0x80;}
    else if((first&0xf0)==0xe0){count=2;code=first&15;minCode=0x800;}
    else if((first&0xf8)==0xf0){count=3;code=first&7;minCode=0x10000;}
    else return '?';
    while(count--){if((uint8_t(*p)&0xc0)!=0x80)return '?';code=(code<<6)|(uint8_t(*p++)&63);}
    if(code<minCode || code>0x10ffff || (code>=0xd800&&code<=0xdfff))return '?';
    return code;
}
inline int zhGlyph(uint32_t code) {
    int lo=0,hi=kZhGlyphCount;
    while(lo<hi){const int mid=(lo+hi)/2;const uint16_t found=pgm_read_word(kZhCodes+mid);if(found==code)return mid;if(found<code)lo=mid+1;else hi=mid;}
    return -1;
}
inline int zhWidth(const char* str,ZhSize size=ZhSize::Body) {
    if(!str)return 0;const int cell=kZhFonts[uint8_t(size)].cell;int width=0;
    while(*str)width+=zhNext(str)<128?cell/2:cell;
    return width;
}
template<class Canvas> void zhText(Canvas& c,int x,int y,const char* str,uint16_t color,ZhSize size=ZhSize::Body) {
    const auto& font=kZhFonts[uint8_t(size)];
    if(!str || y+font.height<=c.top() || y>=c.top()+16)return;
    const int first=y<c.top()?c.top()-y:0,last=y+font.height>c.top()+16?c.top()+16-y:font.height;
    while(*str) {
        const uint32_t code=zhNext(str);int glyph=zhGlyph(code);const int advance=code<128?font.cell/2:font.cell;
        if(glyph<0){++zhMissingGlyphs;glyph=zhGlyph('?');}
        const uint8_t* bits=font.bits+glyph*font.height*font.stride;
        for(int row=first;row<last;++row)for(int col=0;col<advance;++col)
            if(pgm_read_byte(bits+row*font.stride+col/8)&(0x80>>(col%8)))c.pixel(x+col,y+row,color);
        x+=advance;
    }
}
template<class Canvas> void zhRight(Canvas& c,int right,int y,const char* str,uint16_t color,ZhSize size=ZhSize::Body) {
    zhText(c,right-zhWidth(str,size)+1,y,str,color,size);
}
inline const char* zhLabStage(uint8_t stage) {
    const char* names[]={"怠速","连续变化","预警","严重报警","回落解除","水温报警","水温缺失","适配器静默","链路断开","链路恢复","有效零值","仅转速预警","仅转速红线"};
    if(stage==13)return "转速爬升";
    return stage<13?names[stage]:"准备中";
}
inline const char* zhElmState(uint8_t state) {
    const char* names[]={"空闲","复位中","能力探测","应用配置","就绪","错误"};
    return state<6?names[state]:"未知";
}
}
