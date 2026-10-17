#pragma once
#include <Preferences.h>
#include "elm_parse.h"
namespace obd {
constexpr uint8_t kCanQueryCount=15;
inline const char* canQueryCommand(uint8_t i){
    static const char* const commands[]={"ATDP\r","ATDPN\r","ATH1\r","0100\r","ATSH7E0\r","0100\r","015C\r","0124\r","0134\r","0144\r","0900\r","0904\r","090A\r","ATSH7DF\r","ATH0\r"};
    return i<kCanQueryCount?commands[i]:"";
}
inline bool canProtocol6(const char* raw){
    for(const char* p=raw;*p;){const char* e=p;while(*e&&*e!='\r'&&*e!='\n')++e;
        while(p<e&&*p==' ')++p;const char* q=e;while(q>p&&q[-1]==' ')--q;
        if((q-p==1&&*p=='6')||(q-p==2&&p[0]=='A'&&p[1]=='6'))return true;
        p=e;while(*p=='\r'||*p=='\n')++p;
    }return false;
}
struct CanQueryRecord {
    uint32_t magic=0x43414E31,boot=0,run=0,ms=0;
    uint8_t step=0;bool prompt=false,truncated=false;char raw[256]{};
};
// Fixed standard read requests only. No arbitrary command, service, address scan,
// ECU session change, security access or data-write service is exposed.
class CanQuery {
public:
    bool available=false,writeOk=true,protocol6=false,restored=false,reinit=false;
    uint8_t error=0;
    bool start(uint32_t now,uint32_t boot){
        if(active_)return false;
        if(!available)available=prefs_.begin("canquery",false);
        if(!available)return false;
        boot_=boot;run_=now;next_=now;step_=0;active_=true;pending_=false;
        protocol6=restored=reinit=false;error=0;writeOk=true;headerRestored_=false;return true;
    }
    bool active()const{return active_;}bool pending()const{return pending_;}
    uint8_t step()const{return step_;}uint32_t run()const{return run_;}
    bool due(uint32_t now){
        if(active_&&!pending_&&uint32_t(now-run_)>30000&&step_<13){error=4;step_=13;}
        return active_&&!pending_&&int32_t(now-next_)>=0;
    }
    const char* command()const{return canQueryCommand(step_);}
    void sent(){pending_=true;}
    void cancel(){if(active_){error=6;reinit=true;}active_=pending_=false;}
    CanQueryRecord load(uint8_t i){CanQueryRecord r,empty;if(!available||i>=kCanQueryCount)return empty;
        char key[5];snprintf(key,sizeof(key),"r%u",i);
        if(prefs_.getBytes(key,&r,sizeof(r))==sizeof(r)&&r.magic==0x43414E31&&r.raw[255]==0)return r;return empty;
    }
    void accept(const char* raw,bool prompt,uint32_t now){
        if(!pending_)return;CanQueryRecord r;r.boot=boot_;r.run=run_;r.ms=now;r.step=step_;r.prompt=prompt;
        r.truncated=strlen(raw)>=sizeof(r.raw);snprintf(r.raw,sizeof(r.raw),"%s",raw);
        char key[5];snprintf(key,sizeof(key),"r%u",step_);
        if(prefs_.putBytes(key,&r,sizeof(r))!=sizeof(r))writeOk=false;
        pending_=false;
        const bool ok=prompt&&strstr(raw,"OK")!=nullptr;
        if(step_==1){protocol6=prompt&&canProtocol6(raw);if(!protocol6){error=1;active_=false;restored=true;return;}}
        if(step_==2&&!ok){error=3;step_=13;next_=now+200;return;}
        if(step_==3&&(!prompt||!strstr(raw,"7E8"))){error=2;step_=13;next_=now+200;return;}
        if(step_==4&&!ok){error=3;step_=13;next_=now+200;return;}
        if(step_==13)headerRestored_=ok;
        if(step_==14){restored=headerRestored_&&ok;if(!restored){error=5;reinit=true;}active_=false;return;}
        if(!prompt&&step_<13){error=4;step_=13;}else ++step_;
        next_=now+200;
    }
private:
    Preferences prefs_;uint32_t boot_=0,run_=0,next_=0;uint8_t step_=0;
    bool active_=false,pending_=false,headerRestored_=false;
};
}
