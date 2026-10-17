#include <cstdio>
#include <cstdlib>
#include "Arduino.h"
#include "app/drive_journal.h"
uint32_t testMillis=0;unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
int main(){
 testFiles["/vehicle-60s-0.bin"]={1,2,3};
 DriveJournal recovered;recovered.begin();VehicleSnapshot empty{};testMillis=1000;
 recovered.tick(empty,1,testMillis,20000,0,"IDLE");CHECK(recovered.flush());
 CHECK(recovered.read(0,10,[](const DriveRecordV2&,uint32_t){})==1);
 DriveJournal recoveredBoot;recoveredBoot.begin();CHECK(recoveredBoot.records()==1);
 testFiles.clear();testMillis=0;
 DriveJournal log;log.begin();VehicleSnapshot s{};
 s.linkUp=true;s.candidateStates[1]=s.candidateStates[3]=s.candidateStates[2]=uint8_t(ProbeState::Valid);s.candidateValues[1]=-40;s.candidateValues[3]=0;s.candidateValues[2]=50;
 for(uint32_t t=1000;t<=241000;t+=1000){
  testMillis=t;for(auto& ms:s.candidateMs)ms=t;log.tick(s,1,t,20000,0,"CONNECTED");
  if(t==60000){CHECK(log.flush());CHECK(log.records()==60);CHECK(testFiles.size()==1);}
  if(t==120000){CHECK(log.flush());CHECK(log.records()==120);CHECK(testFiles.size()==2);}
  if(t==121000){CHECK(log.flush());CHECK(log.records()==61);CHECK(log.oldest()==61);}
 }
 CHECK(log.flush());CHECK(testFiles.size()==2);CHECK(log.oldest()==181);CHECK(log.records()==61);CHECK(log.dropped==0);
 log.read(0,255,[&](const DriveRecordV2& r,uint32_t row){CHECK(r.validV2());CHECK(r.seq==181+row);CHECK(r.candidateMask==7);CHECK(r.candidates[0]==-40);CHECK(r.candidates[1]==0);});
 DriveJournal reboot;reboot.begin();CHECK(reboot.nextSeq==242);testMillis=500;reboot.tick(s,2,testMillis,20000,0,"CONNECTED");CHECK(reboot.flush());CHECK(reboot.records()==2);
 testFiles["/vehicle-v1-0.bin"]={1};testFiles["/bench-v2-0.bin"]={2};testFiles["user-data"]={3};testNvs["settings"]={4};
 CHECK(reboot.clearLogs());CHECK(reboot.records()==0);CHECK(testFiles.size()==1);CHECK(testFiles["user-data"][0]==3);CHECK(testNvs["settings"][0]==4);CHECK(testFormats==0);
 testFiles.clear();DriveJournal failed;failed.begin();testMillis=1000;failed.tick(s,1,testMillis,20000,0,"CONNECTED");testBufferedWriteLost=true;CHECK(!failed.flush());CHECK(!failed.available);testBufferedWriteLost=false;
 testFiles.clear();DriveJournal dense;dense.begin();for(uint32_t t=250;t<=180250;t+=250){testMillis=t;s.linkUp=!s.linkUp;dense.tick(s,1,t,20000,0,"CONNECTED");}
 CHECK(dense.flush());CHECK(testFiles.size()==2);CHECK(dense.records()<=480);CHECK(dense.oldest()>1);
 testFiles.clear();DriveJournal wrap;wrap.begin();testMillis=0xffffff00u;wrap.tick(s,1,testMillis,20000,0,"IDLE");testMillis+=60000;wrap.tick(s,1,testMillis,20000,0,"IDLE");CHECK(wrap.flush());CHECK(testFiles.size()==2);
 printf("PASS %u journal checks: 60-second segments, two-file wrap, reboot, cleanup, verified commit, dense changes and rollover\n",checks);
}
