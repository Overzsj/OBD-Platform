#pragma once
#include "elm_parse.h"
#include <ctype.h>
namespace obd {
enum class ProbeState:uint8_t { NotRun,Pending,Valid,NoData,Timeout,Rejected,Malformed };
struct ProbeResult {
    uint32_t magic=0x50524F31,boot=0,ms=0;
    float value=NAN;
    ProbeState state=ProbeState::NotRun;
    bool prompt=false,truncated=false;
    char raw[192]{};
};
constexpr uint8_t kProbeCount=10;
inline const char* probeCommand(uint8_t i){
    static const char* const commands[]={"0100\r","0120\r","0140\r","0142\r","015C\r","ATRV\r","0144\r","010F\r","0104\r","0110\r"};
    return i<kProbeCount?commands[i]:"";
}
inline bool parseAdapterVoltage(const char* raw,float& value){
    // A complete decimal voltage line, optionally preceded by an echoed AT RV.
    for(const char* line=raw;*line;){
        const char* end=line;while(*end&&*end!='\r'&&*end!='\n')++end;
        while(line<end&&isspace((unsigned char)*line))++line;
        if(line<end&&isdigit((unsigned char)*line)){
            char* tail=nullptr;const float v=strtof(line,&tail);
            while(tail<end&&*tail==' ')++tail;
            if(tail<end&&(*tail=='V'||*tail=='v')){
                ++tail;while(tail<end&&isspace((unsigned char)*tail))++tail;
                if(tail==end&&isfinite(v)&&v>=0&&v<=32){value=v;return true;}
            }
        }
        line=end;while(*line=='\r'||*line=='\n')++line;
    }
    return false;
}
inline bool parseSupport40(const char* raw,uint32_t& bits){
    char hex[512];stripToHex(raw,hex,sizeof(hex));bool found=false;bits=0;
    for(const char* p=hex;strlen(p)>=12;++p)if(!strncmp(p,"4140",4)){
        uint32_t mask=0;for(int i=4;i<12;++i)mask=(mask<<4)|hexNibble(p[i]);bits|=mask;found=true;
    }
    return found;
}
class SupportProbe {
public:
    ProbeResult results[kProbeCount]{};
    void start(uint32_t now,uint32_t boot,uint32_t interval=2000){
        interval_=interval;boot_=boot;index_=0;pending_=false;active_=true;next_=now+interval_;cycle_=5;
        for(auto& r:results)r=ProbeResult{};
        memset(retryAt_,0,sizeof(retryAt_));memset(failures_,0,sizeof(failures_));cursor_=0;
        results[3].state=results[4].state=results[5].state=ProbeState::Pending;
    }
    void cancel(){active_=pending_=false;}
    bool pending()const{return pending_;}
    bool due(uint32_t now){
        if(!active_||pending_||int32_t(now-next_)<0)return false;
        if(index_<kProbeCount)return true;
        static const uint8_t order[]={7,8,9,7,8,9,5,6};
        for(uint8_t n=0;n<8;++n){uint8_t pos=(cursor_+n)%8,id=order[pos];
            if(!failures_[id]||int32_t(now-retryAt_[id])>=0){cycle_=id;selected_=pos;return true;}}
        return false;
    }
    uint8_t current()const{return index_<kProbeCount?index_:cycle_;}
    bool initial()const{return index_<kProbeCount;}
    const char* command()const{return probeCommand(current());}
    bool refreshable()const{for(uint8_t i=5;i<kProbeCount;++i)if(results[i].state==ProbeState::Valid)return true;return false;}
    void sent(){pending_=true;if(index_>=kProbeCount)cursor_=(selected_+1)%8;}
    void accept(const char* raw,bool prompt,uint32_t now){
        if(!pending_)return;
        const uint8_t id=current();ProbeResult r;r.boot=boot_;r.ms=now;r.prompt=prompt;
        r.truncated=strlen(raw)>=sizeof(r.raw);snprintf(r.raw,sizeof(r.raw),"%s",raw);
        const ReplyKind kind=classify(raw);
        r.state=!prompt?ProbeState::Timeout:kind==ReplyKind::NoData?ProbeState::NoData:
            kind==ReplyKind::Unsupported?ProbeState::Rejected:ProbeState::Malformed;
        if(prompt&&(kind==ReplyKind::Data||kind==ReplyKind::Searching)){
            if(id<3){
                char hex[512],tag[5];stripToHex(raw,hex,sizeof(hex));snprintf(tag,sizeof(tag),"41%02X",id*32);
                for(const char* p=hex;strlen(p)>=12;++p)if(!strncmp(p,tag,4)){r.state=ProbeState::Valid;break;}
            }else if(id<5){
                ParsedPid parsed[8];const char* payload=kind==ReplyKind::Searching?strstr(raw,"41"):raw;
                const uint8_t count=payload?parsePidReply(payload,kPidTable,kPidCount,parsed,8):0;
                for(uint8_t i=0;i<count;++i)if(parsed[i].pid==(id==3?0x42:0x5C)&&isfinite(parsed[i].value)){
                    r.value=parsed[i].value;r.state=ProbeState::Valid;break;
                }
            }else if(id==5){if(parseAdapterVoltage(raw,r.value))r.state=ProbeState::Valid;}
            else {
                const uint8_t pids[]={0x44,0x0F,0x04,0x10};
                // Match the requested PID only; unrelated samples never validate a candidate.
                ParsedPid parsed[8];const char* payload=kind==ReplyKind::Searching?strstr(raw,"41"):raw;
                const uint8_t count=payload?parsePidReply(payload,kPidTable,kPidCount,parsed,8):0;
                for(uint8_t i=0;i<count;++i)if(parsed[i].pid==pids[id-6]&&isfinite(parsed[i].value)){
                    r.value=parsed[i].value;r.state=ProbeState::Valid;break;
                }
            }
        }
        if(r.truncated)r.state=ProbeState::Malformed;
        results[id]=r;pending_=false;
        if(index_<kProbeCount)++index_;
        // Failed fields remain eligible with bounded exponential backoff.
        if(r.state==ProbeState::Valid){failures_[id]=0;retryAt_[id]=now;}
        else {if(failures_[id]<5)++failures_[id];retryAt_[id]=now+(failures_[id]>=5?30000u:(1000u<<failures_[id]));}
        next_=now+interval_;
    }
    int8_t reported(uint8_t pid)const{
        uint32_t bits;if(results[2].state!=ProbeState::Valid||!parseSupport40(results[2].raw,bits)||pid<=0x40||pid>0x60)return -1;
        return (bits&(1u<<(0x60-pid)))?1:0;
    }
private:
    uint32_t retryAt_[kProbeCount]{};uint8_t failures_[kProbeCount]{},cursor_=0,selected_=0;
    uint32_t boot_=0,next_=0,interval_=2000;uint8_t index_=kProbeCount,cycle_=5;bool pending_=false,active_=false;
};
}
