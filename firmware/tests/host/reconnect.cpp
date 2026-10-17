#include <cstdlib>
#define assert(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#include <cstdio>
#include <cstring>
#include "obd/elm_client.h"
#include "obd/link_recovery.h"
uint32_t testMillis=0;
using namespace obd;
struct Wire:ElmClient::Io {bool up=true;unsigned writes=0;char last[64]{};bool connected()override{return up;}size_t write(const char* s,size_t n)override{++writes;memcpy(last,s,n);last[n]=0;return n;}};
static void feed(ElmClient& e,const char* s){e.feed((const uint8_t*)s,strlen(s));}
int main(){
 LinkRecovery r;
 assert(r.checkTransport(0,true,0,false)==RecoveryAction::None);
 // Re-entering Ready after AT timeouts cannot renew the RX deadline.
 for(unsigned t=100;t<8000;t+=100){r.sessionStarted(t);assert(r.checkTransport(t,true,0,false)==RecoveryAction::None);}
 assert(r.checkTransport(8000,true,0,false)==RecoveryAction::Reconnect);
 r.transportDisconnected();assert(r.checkTransport(9000,false,0,false)==RecoveryAction::None);
 assert(r.checkTransport(53999,false,0,false)==RecoveryAction::None);
 assert(r.checkTransport(54000,false,0,false)==RecoveryAction::Reconnect);
 r.transportDisconnected();assert(r.checkTransport(0,true,0,false)==RecoveryAction::None);
 assert(r.checkTransport(7000,true,10,false)==RecoveryAction::None);
 assert(r.checkTransport(14999,true,10,false)==RecoveryAction::None);
 assert(r.checkTransport(15000,true,10,false)==RecoveryAction::Reconnect);
 r.transportDisconnected();r.checkTransport(0,true,0,true);
 assert(r.checkTransport(30001,true,0,true)==RecoveryAction::None);
 assert(r.checkTransport(45000,true,0,true)==RecoveryAction::Reconnect);
 r.transportDisconnected();r.checkTransport(0xfffffff0u,true,0,false);
 assert(r.checkTransport(7983,true,0,false)==RecoveryAction::None);
 assert(r.checkTransport(7984,true,0,false)==RecoveryAction::Reconnect);
 // Replay unplug with the transport stuck connected, then replug and reinit.
 ElmClient elm;PidScheduler sched;Wire io;LinkRecovery recovery;sched.begin();elm.begin();unsigned recoveries=0,parsed=0;
 for(testMillis=0;testMillis<35000;testMillis+=10){
  if(testMillis>=10000&&testMillis<12000){io.up=false;recovery.transportDisconnected();}
  else io.up=true;
  unsigned previous=io.writes;elm.tick(io,testMillis);
  bool adapterPresent=testMillis<1000||testMillis>=12000;
  if(adapterPresent&&io.writes!=previous)feed(elm,strstr(io.last,"ATZ")?"ELM327 v1.5\r>":"OK\r>");
  if(!io.up)continue;
  bool ready=elm.state()==ElmState::Ready;
  if(ready&&sched.entry(0).enabled==false)sched.begin();
  auto action=recovery.checkTransport(testMillis,ready,elm.bytesIn(),elm.searching());
  if(action==RecoveryAction::Reconnect){++recoveries;elm.requestReinit();recovery.transportDisconnected();io.up=false;continue;}
  previous=io.writes;
  if(ready)elm.pollRaw(io,"010C\r",testMillis);
  if(adapterPresent&&io.writes!=previous)feed(elm,"410C0FA0\r>");
  if(elm.replyReady(testMillis)){ParsedPid p[8];parsed+=elm.consumeReply(testMillis,sched,p,8);}
 }
 assert(recoveries>=1);assert(parsed>10);assert(elm.state()==ElmState::Ready);
 printf("PASS RX watchdog: stale connected transport, reinit starvation, absent adapter, SEARCHING grace, activity renewal, rollover, unplug/replug replay (%u recoveries, %u parsed)\n",recoveries,parsed);
}
