#include <cstdio>
#include <cstdlib>
#include "Arduino.h"
#include "app/drive_journal.h"
uint32_t testMillis=0;using namespace obd;
#define CHECK(x) do{if(!(x)){printf("FAIL %d\n",__LINE__);exit(1);}}while(0)
int main(){DriveJournal log;log.begin();CHECK(log.available);CHECK(!log.enabled);CHECK(!log.setEnabled(true));VehicleSnapshot s{};
 for(testMillis=250;testMillis<300000;testMillis+=250){CHECK(!log.due(testMillis));log.tick(s,1,testMillis,20000,3,"FAKE");}
 CHECK(log.flush());CHECK(log.records()==0);CHECK(testFiles.empty());puts("PASS simulated sources cannot record or enable journal over five minutes");}
