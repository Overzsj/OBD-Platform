#include <cstdio>
#include <cstdlib>
#include "Arduino.h"
#include "test/fake_elm.h"
#include "app/vehicle_state.h"
uint32_t testMillis=0;
using namespace obd;
#define CHECK(x) do{if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(){
 VehicleSnapshot audible{};audible.alarmFlags=0x33;audible.testStage=13;CHECK(audibleSnapshotFlags(audible)==0x22);
 audible.testStage=1;CHECK(audibleSnapshotFlags(audible)==0x22);audible.testStage=5;CHECK(audibleSnapshotFlags(audible)==0x33);
 for(uint8_t stage: {0,1,10,11,12,7,8}){
  testMillis=0;FakeElmIo io;ElmClient elm;PidScheduler sched;SupportProbe probe;VehicleSnapshot s{};
  io.begin();io.labControl(stage,0);elm.begin();sched.begin();ElmState last=ElmState::Idle;
  for(testMillis=5;testMillis<20000;testMillis+=5){
   if(testMillis==10000){io.labControl(stage,testMillis);elm.resetSimulatedTransaction();sched.begin();probe.cancel();
    CHECK(!elm.awaiting());if(elm.state()==ElmState::Ready)probe.start(testMillis,1,200);}
   io.tick(testMillis);elm.tick(io,testMillis);auto state=elm.state();
   if(state!=last){if(state==ElmState::Ready)probe.start(testMillis,1,200);last=state;}
   while(io.available()){uint8_t b=io.read();elm.feed(&b,1);}
   s.linkUp=io.isConnected()&&elm.state()==ElmState::Ready;
   if(!elm.awaiting()&&probe.due(testMillis)){if(elm.pollRaw(io,probe.command(),testMillis))probe.sent();}
   else elm.poll(io,sched,testMillis);
   if(elm.replyReady(testMillis)){
    ParsedPid p[8];elm.consumeReply(testMillis,sched,p,8);
    if(probe.pending()){probe.accept(elm.lastReply(),elm.lastReplyHadPrompt(),testMillis);syncProbeSnapshot(probe,s);}
   }
  }
  for(uint8_t i: {1,2,3})CHECK(candidateFresh(s,i,testMillis)==(stage!=7&&stage!=8));
  if(stage==0){CHECK(s.candidateValues[1]==27);CHECK(fabs(s.candidateValues[3]-2.9f)<.01f);}
  if(stage==10){CHECK(s.candidateValues[1]==-40);CHECK(s.candidateValues[2]==0);CHECK(s.candidateValues[3]==0);}
  if(stage==11||stage==12){auto expected=labSample(stage,0);CHECK(s.candidateValues[1]==expected.intake);CHECK(s.candidateValues[3]==expected.maf);CHECK(fabs(s.candidateValues[2]-expected.load)<.4f);}
 }
 puts("PASS simulated main metrics: fake ELM -> raw query -> parser -> freshness, idle/ramp/high-RPM/zero/silence/disconnect");
}
