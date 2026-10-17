#include <cstdio>
#include <cstdlib>
#include <vector>
#include "Arduino.h"
#include "ui/runtime_theme.h"
#include "ui/boot_animation.h"
uint32_t testMillis=0;unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
uint32_t frame(){uint16_t pixels[480*16];TftCanvas c;uint32_t h=0;for(int t=0;t<3800;t+=100)for(int y=0;y<320;y+=16){c.attach(pixels,y);BootAnimation::draw(c,t);for(auto p:pixels)h=(h^p)*16777619;}return h;}
int main(int argc,char** argv){
 CHECK(argc==2);FILE* f=fopen(argv[1],"rb");CHECK(f);fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);std::vector<uint8_t> bytes(n);CHECK(fread(bytes.data(),1,n,f)==n);fclose(f);
 uint32_t original=frame();CHECK(g_runtimeTheme.load(bytes.data(),n));CHECK(!strcmp(g_runtimeTheme.status,"external"));CHECK(frame()==original);
 const auto* previous=suki_assets::kBackground;auto good=bytes;bytes[500]^=1;CHECK(!g_runtimeTheme.load(bytes.data(),n));CHECK(suki_assets::kBackground==previous);bytes=good;
 // Recomputed CRC still must not permit an out-of-bounds resource or binding.
 bytes[28]=1;uint32_t crc=RuntimeTheme::crc(bytes.data()+20,n-20);memcpy(bytes.data()+16,&crc,4);CHECK(!g_runtimeTheme.load(bytes.data(),n));bytes=good;
 uint32_t off=RuntimeTheme::u32(bytes.data()+24+21*12+4);bytes[off]=255;crc=RuntimeTheme::crc(bytes.data()+20,n-20);memcpy(bytes.data()+16,&crc,4);CHECK(!g_runtimeTheme.load(bytes.data(),n));bytes=good;
 bytes[off]=uint8_t(Signal::Speed);crc=RuntimeTheme::crc(bytes.data()+20,n-20);memcpy(bytes.data()+16,&crc,4);CHECK(g_runtimeTheme.load(bytes.data(),n));CHECK(kThemeSlots[0]==Signal::Speed);
 printf("PASS %u runtime-theme assertions incl pixel equivalence and corrupted package rejection\n",checks);
}
