#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "obd/can_query.h"
uint32_t testMillis=0;static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
void reply(CanQuery& q,const char* raw,uint32_t& now,bool prompt=true){CHECK(q.due(now));q.sent();CHECK(q.pending());q.accept(raw,prompt,now);CHECK(!q.pending());now+=200;}
int main(){
 CHECK(canProtocol6("ATDPN\rA6\r"));CHECK(canProtocol6("6\r"));CHECK(!canProtocol6("A7\r"));CHECK(!canProtocol6("ISO 15765-4 CAN 11/500\r"));
 for(uint8_t i=0;i<kCanQueryCount;++i){const char* c=canQueryCommand(i);CHECK(*c);CHECK(!strstr(c,"22"));CHECK(!strstr(c,"04\r")||!strcmp(c,"0904\r"));}
 CanQuery q;CHECK(q.start(100,4));CHECK(!q.start(100,4));uint32_t now=100;
 reply(q,"AUTO, ISO 15765-4 (CAN 11/500)\r",now);reply(q,"A6\r",now);reply(q,"OK\r",now);reply(q,"7E8 06 41 00 BE 3F B8 13 00\r",now);reply(q,"OK\r",now);
 reply(q,"7E8 06 41 00 BE 3F B8 13 00\r",now);reply(q,"NO DATA\r",now);reply(q,"NO DATA\r",now);reply(q,"NO DATA\r",now);reply(q,"7E8 04 41 44 80 00\r",now);
 reply(q,"7E8 06 49 00 14 40 00 00\r",now);reply(q,"NO DATA\r",now);reply(q,"NO DATA\r",now);reply(q,"OK\r",now);reply(q,"OK\r",now);
 CHECK(!q.active()&&q.restored&&q.protocol6&&!q.error&&!q.reinit);CHECK(q.load(6).boot==4&&!strcmp(q.load(6).raw,"NO DATA\r"));
 CHECK(q.start(100,5));now=100;reply(q,"ISO 9141\r",now);reply(q,"A3\r",now);CHECK(!q.active()&&q.error==1&&q.restored);
 CHECK(q.start(100,6));now=100;reply(q,"CAN\r",now);reply(q,"A6\r",now);reply(q,"OK\r",now);reply(q,"7E9 06 41 00 BE 3F B8 13\r",now);CHECK(q.step()==13&&q.error==2);
 reply(q,"OK\r",now);reply(q,"OK\r",now);CHECK(q.restored);
 CHECK(q.start(100,7));now=100;reply(q,"",now,false);CHECK(q.step()==13&&q.error==4);reply(q,"?",now);reply(q,"?",now);CHECK(!q.active()&&!q.restored&&q.reinit&&q.error==5);
 CHECK(q.start(0xfffffff0u,8));now=0xfffffff0u;reply(q,"CAN\r",now);CHECK(q.due(now));q.cancel();CHECK(!q.active()&&q.error==6);
 CHECK(q.start(0,9));CHECK(q.due(30001)&&q.step()==13&&q.error==4);q.cancel();
 printf("PASS %u CAN query assertions: fixed reads, protocol/ECU guards, response archive, restoration, rejection, timeout, disconnect, rollover\n",checks);
}
