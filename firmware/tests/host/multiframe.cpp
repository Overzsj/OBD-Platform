#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "obd/elm_client.h"
uint32_t testMillis=0;static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
struct Wire:ElmClient::Io{char last[64]{};unsigned writes=0;size_t write(const char* p,size_t n)override{memcpy(last,p,n);last[n]=0;++writes;return n;}bool connected()override{return true;}};
void ready(ElmClient& e,Wire& w){e.begin();for(int i=0;i<100&&e.state()!=ElmState::Ready;++i){const unsigned before=w.writes;e.tick(w,testMillis);if(w.writes!=before){const char* s=strstr(w.last,"ATZ")?"ELM327 v1.5\r>":"OK\r>";e.feed((const uint8_t*)s,strlen(s));}testMillis+=10;}CHECK(e.state()==ElmState::Ready);}
float value(ParsedPid* p,uint8_t n,uint8_t pid){for(uint8_t i=0;i<n;++i)if(p[i].pid==pid)return p[i].value;return NAN;}
int main(){
 const char* a="00A\r0:410C0CC10D00\r1:111C05641C0562\r\r";
 const char* b="00B\r0:414238A50C0C\r1:920D00111C0562\r\r";
 const char* c="008\r0:410D00111C42\r1:381300111C0562\r\r";
 ParsedPid p[8];uint8_t n=parsePidReply(a,kPidTable,kPidCount,p,8);CHECK(n==4);CHECK(value(p,n,0x0C)==816.25f);CHECK(value(p,n,0x05)==60);CHECK(fabs(value(p,n,0x11)-10.980392f)<.001);
 n=parsePidReply(b,kPidTable,kPidCount,p,8);CHECK(n==4);CHECK(value(p,n,0x0C)==804.5f);CHECK(fabs(value(p,n,0x42)-14.501f)<.001);CHECK(fabs(value(p,n,0x11)-10.980392f)<.001);
 n=parsePidReply(c,kPidTable,kPidCount,p,8);CHECK(n==3);CHECK(fabs(value(p,n,0x42)-14.355f)<.001);CHECK(fabs(value(p,n,0x11)-10.980392f)<.001);CHECK(isnan(value(p,n,0x05)));
 CHECK(parsePidReply("SEARCHING...\r00A\r0:410C0CC10D00\r1:111C05641C0562\r",kPidTable,kPidCount,p,8)==4);
 CHECK(parsePidReply(" 00a\n0: 41 0c 0c c1 0d 00\n1: 11 1c 05 64\n",kPidTable,kPidCount,p,8)==4);
 for(const char* bad:{"00A\r0:410C0CC10D00\r","00A\r0:410C0CC10D00\r2:111C0564\r","00A\r1:410C0CC10D00\r0:111C0564\r","0:410C0CC10D00\r1:111C0564\r","00A\r0:410C0CC10D0\r1:111C0564\r","00A\r0:410C0CC10D00\r1:11XX0564\r","FFF\r0:410C0CC10D00\r","000\r0:410C0CC10D00\r","00A\r0:410C0CC10D00\r0:111C0564\r"})CHECK(parsePidReply(bad,kPidTable,kPidCount,p,8)==0);
 // The two data bytes can straddle numbered lines; sequence digits must not enter them.
 CHECK(parsePidReply("004\r0:414238\r1:13\r",kPidTable,kPidCount,p,8)==1);CHECK(fabs(p[0].value-14.355f)<.001);
 CHECK(parsePidReply("41423813\r",kPidTable,kPidCount,p,8)==1&&fabs(p[0].value-14.355f)<.001);
 char small[12];CHECK(pidPayloadHex(a,small,sizeof(small))==0);
 ElmClient e;Wire w;ready(e,w);PidScheduler sched;sched.begin();
 for(const char* raw:{a,b,c}){
  CHECK(e.pollRaw(w,"01 0C 0D 11 05\r",testMillis));
  for(const char* q=raw;*q;++q){e.feed((const uint8_t*)q,1);CHECK(!e.replyReady(testMillis));++testMillis;}
  const uint8_t prompt='>';e.feed(&prompt,1);CHECK(e.replyReady(testMillis));
  n=e.consumeReply(testMillis,sched,p,8);CHECK(n>=3);CHECK(fabs(value(p,n,0x11)-10.980392f)<.001);CHECK(e.lastReplyHadPrompt());
 }
 printf("PASS %u multiframe assertions: actual car replies, declared length/padding, sequence, truncation, fragmented client ingestion\n",checks);
}
