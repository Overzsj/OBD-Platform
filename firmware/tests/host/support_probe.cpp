#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "app/probe_archive.h"
#include "app/vehicle_state.h"
#include "app/response_trace.h"
#include "ui/data_lab_theme.h"
uint32_t testMillis=0;static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
void response(SupportProbe& p,const char* raw,bool prompt,uint32_t& now){CHECK(p.due(now));p.sent();CHECK(p.pending());p.accept(raw,prompt,now);CHECK(!p.pending());now+=2000;}
int main(){
 ParsedPid maf[1];CHECK(parsePidReply("41102710",kPidTable,kPidCount,maf,1)==1&&maf[0].value==100.0f);
 float volts=0;
 CHECK(parseAdapterVoltage("AT RV\r13.9V\r",volts)&&fabs(volts-13.9)<.001);
 CHECK(parseAdapterVoltage("\n  12.45 v  \r",volts));
 for(const char* bad:{"OK","?","13.8","33.0V","-1V","13.9V garbage","410C0FA0","nanV","INFV"})CHECK(!parseAdapterVoltage(bad,volts));
 uint32_t bits=0;CHECK(parseSupport40("7E8 06 41 40 40 00 00 10\r",bits)&&bits==0x40000010);
 CHECK(!parseSupport40("41404000",bits));
 SupportProbe p;CHECK(!p.due(10000));p.start(0,7);CHECK(!p.due(1999));uint32_t now=2000;
 response(p,"410080000001\r",true,now);response(p,"412000000001\r",true,now);response(p,"414040000010\r",true,now);
 CHECK(p.reported(0x42)==1&&p.reported(0x5C)==1);
 response(p,"41 42 36 B0\r",true,now);CHECK(p.results[3].state==ProbeState::Valid&&fabs(p.results[3].value-14.0)<.001);
 response(p,"SEARCHING...\r415C7D\r",true,now);CHECK(p.results[4].state==ProbeState::Valid&&p.results[4].value==85);
 response(p,"ATRV\r13.8V\r",true,now);CHECK(p.results[5].state==ProbeState::Valid&&p.initial());
 response(p,"41448000\r",true,now);CHECK(p.results[6].state==ProbeState::Valid&&p.results[6].value==1.0);
 response(p,"410F50\r",true,now);CHECK(p.results[7].value==40);
 response(p,"410480\r",true,now);CHECK(fabs(p.results[8].value-50.196)<.001);
 response(p,"411001F4\r",true,now);CHECK(p.results[9].value==5.0&&!p.initial());
 CHECK(!p.due(now-1));CHECK(p.due(now));CHECK(p.current()==7);
 response(p,"?",true,now);CHECK(p.results[7].state==ProbeState::Rejected);CHECK(p.due(now));
 bool transientRecovered=false;
 for(int attempt=0;attempt<40;++attempt){
  if(p.due(now)){uint8_t id=p.current();p.sent();p.accept(id==7?"410F50\r":"NO DATA\r",true,now);if(id==7){transientRecovered=true;break;}}
  now+=1000;
 }
 CHECK(transientRecovered&&p.results[7].state==ProbeState::Valid);
 p.start(0,8);now=2000;for(int i=0;i<3;++i)response(p,"414000000000\r",true,now);
 CHECK(p.reported(0x42)==0&&p.reported(0x5C)==0);
 response(p,"NO DATA\r",true,now);response(p,"",false,now);response(p,"OK\r",true,now);
 CHECK(p.results[3].state==ProbeState::NoData);CHECK(p.results[4].state==ProbeState::Timeout);CHECK(p.results[5].state==ProbeState::Malformed);
 for(int i=0;i<4;++i)response(p,"NO DATA\r",true,now);
 CHECK(p.due(now)); // Failed candidates are retried, not permanently discarded.
 bool retryRecovered=false;
 for(int i=0;i<80;++i){
  if(p.due(now)){uint8_t id=p.current();p.sent();p.accept(id==7?"410F50\r":"NO DATA\r",true,now);if(id==7){retryRecovered=true;break;}}
  now+=1000;
 }
 CHECK(retryRecovered&&p.results[7].state==ProbeState::Valid);
 VehicleSnapshot s{};
 // Use the same producer adapter as main.cpp, including the actual car mismatch.
 p.results[3].state=ProbeState::Valid;p.results[4].state=ProbeState::NoData;
 syncProbeSnapshot(p,s);s.oilReported=0;s.linkUp=true;
 CHECK(!strcmp(probeWord(s,VF_OIL_TEMP),"未报告支持"));
 CHECK(s.probeStates[1]==uint8_t(ProbeState::Valid));
 p.results[6].state=ProbeState::Valid;p.results[6].value=1.0;p.results[6].ms=100;
 syncProbeSnapshot(p,s);CHECK(candidateFresh(s,0,15100));CHECK(!candidateFresh(s,0,15101));
 s.linkUp=false;CHECK(!candidateFresh(s,0,100));
s.linkUp=true;s.probeStates[0]=uint8_t(ProbeState::Timeout);CHECK(!strcmp(probeWord(s,VF_OIL_TEMP),"读取超时"));
 s.probeStates[1]=uint8_t(ProbeState::NoData);s.voltsReported=0;CHECK(!strcmp(probeWord(s,VF_VOLTS),"未报告支持"));
 s.adapterVoltsValid=true;s.adapterVolts=13.8;s.adapterVoltsMs=100;CHECK(adapterVoltageFresh(s,5100));CHECK(!adapterVoltageFresh(s,5101));CHECK(!valueFresh(s,VF_VOLTS,100));
 p.start(0xfffffff0u,9);CHECK(!p.due(1983));CHECK(p.due(1984));p.sent();p.cancel();CHECK(!p.pending()&&!p.due(9000));
 ProbeResult saved;saved.boot=42;saved.state=ProbeState::Valid;saved.value=14.0;strcpy(saved.raw,"414236B0");
 ProbeArchive a;a.begin();a.save(3,saved);ProbeArchive b;b.begin();CHECK(b.load(3).magic==0x50524F31&&b.load(3).boot==42&&b.load(3).value==14.0&&!strcmp(b.load(3).raw,"414236B0"));
 testWriteFailure=true;a.save(3,p.results[3]);CHECK(!a.writeOk);testWriteFailure=false;
 ResponseArchive trace;trace.begin();VehicleSnapshot before{};before.linkUp=true;
 before.rpm=1000;before.coolantC=48;before.volts=14.575;before.throttle=11;
 before.validMask=(1<<VF_RPM)|(1<<VF_COOLANT)|(1<<VF_VOLTS)|(1<<VF_THROTTLE);
 for(auto& ms:before.valueMs)ms=100;
 ParsedPid v{};v.pid=0x05;v.value=49;float previous=NAN;CHECK(traceReason(v,before,200,previous)==0);
 v.value=-19;CHECK(trace.observe(&v,1,before,107,200,"01 05\r","410515\r",true));
 CHECK(trace.load(0).previous==48&&trace.load(0).value==-19&&trace.load(0).pid==5);
 ResponseArchive restored;restored.begin();CHECK(restored.load(0).boot==107&&!strcmp(restored.load(0).raw,"410515\r"));
 v.pid=0x42;v.value=5.006;CHECK(traceReason(v,before,200,previous)==1);
 CHECK(traceReason(v,before,6000,previous)==0); // stale RPM cannot establish running engine
 v.pid=0x11;v.value=81;CHECK(traceReason(v,before,200,previous)==2);
 for(int i=0;i<3;++i)CHECK(trace.observe(&v,1,before,107,200,"01 11\r","4111CF\r",true));
 const unsigned writesBefore=testWrites;CHECK(!trace.observe(&v,1,before,107,200,"01 11\r","4111CF\r",true));CHECK(testWrites==writesBefore);
 CHECK(!profilePidEnabled(0x44));CHECK(!profilePidEnabled(0x10)); // probe candidates never join the fast batch
 printf("PASS %u support-probe checks: response matching, bitmaps, conversions, ATRV, no-data/timeout, voltage source isolation, archive, rollover\n",checks);
}

