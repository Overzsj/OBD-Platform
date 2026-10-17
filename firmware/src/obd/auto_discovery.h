#pragma once
#include "elm_parse.h"
#include <Preferences.h>
namespace obd {
// One bounded read-only discovery sequence on every session. Cached results
// are hints only: never reuse support bits as authority on an unidentified car.
class AutoDiscovery {
public:
 uint32_t masks[3]{};bool known[3]{};char vin[18]{},protocol[48]{};bool cacheMatch=false;
 void start(uint32_t now){step_=0;pending_=false;next_=now;memset(masks,0,sizeof(masks));memset(known,0,sizeof(known));vin[0]=protocol[0]=0;cacheMatch=false;}
 void cancel(){step_=5;pending_=false;}
 bool due(uint32_t now)const{return step_<5&&!pending_&&int32_t(now-next_)>=0;}
 bool pending()const{return pending_;}bool complete()const{return step_==5;}
 const char* command()const{static const char* c[]={"0100\r","0120\r","0140\r","0902\r","ATDP\r"};return step_<5?c[step_]:"";}
 void sent(){pending_=true;}
 int8_t reported(uint8_t pid)const{if(!pid||pid>0x60)return -1;uint8_t page=(pid-1)/32;return known[page]?((masks[page]&(1u<<(31-(pid-1)%32)))?1:0):-1;}
 static bool bitmap(const char* raw,uint8_t base,uint32_t& bits){
  // Parse each line separately; never build a synthetic mask across ECU lines.
  bool found=false;bits=0;char tag[5];snprintf(tag,sizeof(tag),"41%02X",base);
  for(const char* line=raw;*line;){const char* end=line;while(*end&&*end!='\r'&&*end!='\n')++end;
   char text[192],hex[192];size_t len=size_t(end-line);if(len>=sizeof(text))return false;memcpy(text,line,len);text[len]=0;size_t n=stripToHex(text,hex,sizeof(hex));
   for(size_t i=0;i+12<=n;++i)if(!strncmp(hex+i,tag,4)){uint32_t v=0;for(size_t k=i+4;k<i+12;++k)v=v<<4|hexNibble(hex[k]);bits|=v;found=true;break;}
   line=end;while(*line=='\r'||*line=='\n')++line;
  }return found;
 }
 void accept(const char* raw,bool prompt,uint32_t now){
  if(!pending_)return;
  if(prompt){
   if(step_<3)known[step_]=bitmap(raw,step_*32,masks[step_]);
   else if(step_==3){
    char hex[512];size_t n=pidPayloadHex(raw,hex,sizeof(hex));
    // Standard service 09 PID 02: message index 01 followed by 17 VIN bytes.
    for(size_t i=0;i+40<=n;++i)if(!strncmp(hex+i,"490201",6)){
     char candidate[18]{};bool good=true;for(int j=0;j<17;++j){char c=(hexNibble(hex[i+6+j*2])<<4)|hexNibble(hex[i+7+j*2]);candidate[j]=c;if(!((c>='A'&&c<='Z'&&c!='I'&&c!='O'&&c!='Q')||(c>='0'&&c<='9')))good=false;}
     if(good){if(vin[0]&&strcmp(vin,candidate)){vin[0]=0;break;}memcpy(vin,candidate,18);}
    }
   }else{size_t k=0;for(const char* c=raw;*c&&k+1<sizeof(protocol);++c)if((*c>='A'&&*c<='Z')||(*c>='0'&&*c<='9')||*c==' '||*c=='('||*c==')'||*c=='-'||*c=='/')protocol[k++]=*c;protocol[k]=0;}
  }
  pending_=false;++step_;next_=now+150;
  if(complete()&&vin[0]&&!kSimulatedSource)saveHint();
 }
private:
 struct Hint{uint32_t version;char vin[18];uint8_t valid[3];uint8_t padding;uint32_t masks[3];};
 void saveHint(){Preferences prefs;if(!prefs.begin("obd-auto",false))return;Hint old{};Hint h{};h.version=1;memcpy(h.vin,vin,18);for(int i=0;i<3;++i){h.valid[i]=known[i];h.masks[i]=masks[i];}
  const bool existing=prefs.getBytesLength("last-car")==sizeof(old)&&prefs.getBytes("last-car",&old,sizeof(old))==sizeof(old);
  cacheMatch=existing&&memcmp(&h,&old,sizeof(h))==0;if(!cacheMatch)prefs.putBytes("last-car",&h,sizeof(h));
 }
 uint8_t step_=5;bool pending_=false;uint32_t next_=0;
};
}
