#pragma once
#include "themes/active_theme.h"
#include "../app/telemetry_signals.h"
#include <cstring>
namespace obd {
// Little-endian fixed ABI, validated completely before publishing any pointer.
class RuntimeTheme {
public:
 const char* status="builtin";const char* identity="suki-ignition@builtin";
 static uint32_t u32(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
 static uint32_t crc(const uint8_t* p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(int b=0;b<8;++b)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
 bool load(const void* memory,size_t capacity){
  const auto* p=static_cast<const uint8_t*>(memory);
  if(capacity<408||memcmp(p,"SUKITH29",8)||u32(p+8)!=1)return reject("bad_header");
  const uint32_t size=u32(p+12),count=u32(p+20);
  if(size<408||size>capacity||count!=24||crc(p+20,size-20)!=u32(p+16))return reject("bad_crc_or_size");
  const uint8_t* parts[24]{};uint32_t lengths[24]{};uint32_t previous=408;
  for(uint32_t i=0;i<count;++i){const auto* d=p+24+i*12;uint32_t id=u32(d),off=u32(d+4),len=u32(d+8);
   if(id!=i+1||(off&3)||off<previous||off>size||len>size-off)return reject("bad_directory");
   parts[i]=p+off;lengths[i]=len;previous=off+len;
  }
  const uint32_t fixed[]={307200,256,9800,9800,4900,64,119600,119600,20160,20160};
  for(int i=0;i<10;++i)if(lengths[i]!=fixed[i])return reject("bad_geometry");
  if(lengths[10]%6||lengths[10]<120||lengths[10]>768)return reject("bad_debug_font");
  for(int f=0;f<4;++f){
   if(!lengths[11+f]||lengths[11+f]>100000||lengths[15+f]%sizeof(UiGlyph)||lengths[15+f]<10*sizeof(UiGlyph)||lengths[15+f]>160*sizeof(UiGlyph))return reject("bad_font");
   const auto* g=reinterpret_cast<const UiGlyph*>(parts[15+f]);const size_t n=lengths[15+f]/sizeof(UiGlyph);
   for(size_t i=0;i<n;++i){if(g[i].width>120||g[i].height>120||g[i].advance>120||g[i].offset>lengths[11+f]||uint32_t(g[i].width)*g[i].height>lengths[11+f]-g[i].offset||(i&&g[i].code<=g[i-1].code))return reject("bad_glyph");}
  }
  if(lengths[19]!=12||lengths[20]!=164||lengths[21]!=5||lengths[22]!=80||lengths[23]!=1||parts[23][0]>1)return reject("bad_metadata");
  for(int i=0;i<5;++i)if(parts[21][i]>=uint8_t(Signal::Count))return reject("bad_binding");
  for(int i=0;i<4;++i)if(!memchr(parts[20]+i*41,0,41))return reject("bad_label");
  if(!memchr(parts[22],0,80))return reject("bad_identity");
  for(uint32_t i=0;i<lengths[20];++i){uint8_t c=parts[20][i];if(c&&(c<32||c>126||c=='"'||c=='\\'))return reject("bad_label");}
  for(uint32_t i=0;i<80;++i){uint8_t c=parts[22][i];if(c&&!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='@'||c=='.'))return reject("bad_identity");}
  suki_assets::kBackground=reinterpret_cast<const uint16_t*>(parts[0]);kUiLedMask=parts[1];
  suki_assets::kFaceCalm=reinterpret_cast<const uint16_t*>(parts[2]);suki_assets::kFaceEffort=reinterpret_cast<const uint16_t*>(parts[3]);suki_assets::kFaceMask=parts[4];suki_assets::kCharacterSine=reinterpret_cast<const int8_t*>(parts[5]);
  suki_assets::kBootCalm=reinterpret_cast<const uint16_t*>(parts[6]);suki_assets::kBootEffort=reinterpret_cast<const uint16_t*>(parts[7]);suki_assets::kBootLogo=parts[8];suki_assets::kBootGlow=parts[9];
  kFont5x7=reinterpret_cast<const Glyph5x7*>(parts[10]);kFont5x7Count=lengths[10]/sizeof(Glyph5x7);
  for(int f=0;f<4;++f)kUiFonts[f]={parts[11+f],reinterpret_cast<const UiGlyph*>(parts[15+f]),uint16_t(lengths[15+f]/sizeof(UiGlyph))};
  const auto* colors=reinterpret_cast<const uint16_t*>(parts[19]);
  theme::kBg=colors[0];theme::kWhite=colors[1];theme::kYellow=colors[2];theme::kMuted=colors[3];theme::kCyan=colors[4];theme::kPink=colors[5];
  theme::kBootTop=reinterpret_cast<const char*>(parts[20]);theme::kBootRight=theme::kBootTop+41;theme::kBootTagline=theme::kBootTop+82;theme::kBootFooter=theme::kBootTop+123;
  for(int i=0;i<5;++i)kThemeSlots[i]=Signal(parts[21][i]);identity=reinterpret_cast<const char*>(parts[22]);theme::kCharacterRpmLinked=parts[23][0];status="external";return true;
 }
private:bool reject(const char* why){status=why;return false;}
};
static_assert(sizeof(UiGlyph)==20,"Theme glyph ABI changed");
static RuntimeTheme g_runtimeTheme;
}
#ifdef ARDUINO
#include <esp_partition.h>
#include <esp_spi_flash.h>
namespace obd {
inline bool mountRuntimeTheme(){
 const esp_partition_t* part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,esp_partition_subtype_t(0x40),"theme");
 if(!part){g_runtimeTheme.status="partition_missing";return false;}
 static spi_flash_mmap_handle_t handle;const void* mapped=nullptr;
 if(esp_partition_mmap(part,0,part->size,SPI_FLASH_MMAP_DATA,&mapped,&handle)!=ESP_OK){g_runtimeTheme.status="map_failed";return false;}
 if(!g_runtimeTheme.load(mapped,part->size)){spi_flash_munmap(handle);return false;}
 return true; // Mapping lives for the complete boot; never hot-swap while drawing.
}
}
#endif
