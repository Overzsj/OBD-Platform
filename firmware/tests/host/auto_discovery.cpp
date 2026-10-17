#include <cstdio>
#include <cstdlib>
#include "Arduino.h"
#include "obd/auto_discovery.h"
#include "app/telemetry_pipeline.h"
#include "app/telemetry_signals.h"
uint32_t testMillis=0;unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
void reply(AutoDiscovery& a,const char* raw,bool prompt=true){CHECK(a.due(testMillis));a.sent();CHECK(a.pending());a.accept(raw,prompt,testMillis);testMillis+=200;}
int main(){
 uint32_t bits=0;CHECK(AutoDiscovery::bitmap("7E8 06 41 00 80 00 00 00\r7E9 06 41 00 00 10 00 00\r",0,bits)&&bits==0x80100000);
 CHECK(!AutoDiscovery::bitmap("41008000\r0000\r",0,bits));
 AutoDiscovery a;a.start(0);CHECK(a.reported(0x0c)==-1);
 reply(a,"4100FFFFFFFF\r");reply(a,"412000000001\r");reply(a,"414040000010\r");
 CHECK(a.reported(0x0c)==1&&a.reported(0x42)==1&&a.reported(0x5c)==1&&a.reported(0x21)==0);
 // Service 09 with CAF1 frame numbering, tested through the real assembler.
 const char* vin="014\r0:4902014A4D4253\r1:4E435833413144\r2:303030303031\r";
 reply(a,vin);CHECK(!strcmp(a.vin,"JMBSNCX3A1D000001"));
 reply(a,"AUTO, ISO 15765-4 (CAN 11/500)\r");CHECK(a.complete()&&!a.cacheMatch);
 a.start(testMillis);reply(a,"4100FFFFFFFF\r");reply(a,"412000000001\r");reply(a,"414040000010\r");reply(a,vin);reply(a,"ISO 15765-4\r");CHECK(a.cacheMatch);
 a.start(testMillis);CHECK(!a.vin[0]&&a.reported(0x42)==-1);for(int i=0;i<5;++i)reply(a,"",false);CHECK(a.complete()&&!a.cacheMatch&&!a.vin[0]);
 VehicleSnapshot s{};s.linkUp=true;s.transportState=1;uint8_t fresh=0;ParsedPid p[3];CHECK(parsePidReply("410F5041102710410480",kPidTable,kPidCount,p,3)==3);
 acceptParsed(p,3,s,fresh,testMillis);CHECK(signalValue(s,Signal::Iat,testMillis).value==40);CHECK(signalValue(s,Signal::Maf,testMillis).value==100);CHECK(signalValue(s,Signal::Load,testMillis).fresh());
 CHECK(!signalValue(s,Signal::OilPressure,testMillis).fresh());CHECK(!signalValue(s,Signal::Iat,testMillis+15001).fresh());s.linkUp=false;CHECK(!signalValue(s,Signal::Maf,testMillis).fresh());
 printf("PASS %u auto-discovery and semantic-signal assertions\n",checks);
}
