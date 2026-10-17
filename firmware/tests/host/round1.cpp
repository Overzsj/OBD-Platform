#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "app/gear_estimator.h"
#include "app/vehicle_state.h"
#include "obd/link_recovery.h"
#include "obd/elm_client.h"
uint32_t testMillis=0;
static int checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1); } } while(0)
using namespace obd;
struct Wire : ElmClient::Io {
    bool connected_=true; char last[64]{};unsigned writes=0;
    size_t write(const char* s,size_t n) override { memcpy(last,s,n);last[n]=0;++writes;return n; }
    bool connected() override { return connected_; }
};
void feed(ElmClient& e,const char* s) {e.feed(reinterpret_cast<const uint8_t*>(s),strlen(s));}
void ready(ElmClient& e,Wire& w,bool respond=true) {
    for (int i=0;i<100 && e.state()!=ElmState::Ready;++i) {
        unsigned before=w.writes;e.tick(w,testMillis);
        if (respond && w.writes!=before) feed(e,strstr(w.last,"ATZ")?"ELM327 v1.5\r>":"OK\r>");
        testMillis+=respond?10:1600;
    }
    CHECK(e.state()==ElmState::Ready);
}
int main() {
    CHECK(profilePidEnabled(0x0C));CHECK(profilePidEnabled(0x0D));CHECK(!profilePidEnabled(0x42));
#if defined(OBD_BT_PC_SIM) && OBD_BT_PC_SIM
    CHECK(profilePidEnabled(0xF0));CHECK(profilePidEnabled(0x5C));
#else
    CHECK(!profilePidEnabled(0xF0));CHECK(!profilePidEnabled(0x5C));
#endif
    GearEstimator gear;
    CHECK(!GearEstimator::configured(kVehicleProfile.gears));
    CHECK(gear.update(kVehicleProfile.gears,3000,60,true,1000)==0);
    // Synthetic calibration, never presented as Mitsubishi ratios.
    GearCalibration c{true,3,4.0f,2.0f,{3,2,1,0,0,0}};
    CHECK(gear.update(c,3000,45,true,1000)==0);
    CHECK(gear.update(c,3000,45,true,1599)==0);
    CHECK(gear.update(c,3000,45,true,1600)==2);
    CHECK(gear.update(c,3000,90,true,1700)==0);
    CHECK(gear.update(c,3000,90,true,2300)==3);
    CHECK(gear.update(c,3000,90,false,2400)==0);
    CHECK(gear.update(c,3000,0,true,3000)==0);
    CHECK(gear.update(c,NAN,90,true,3100)==0);
    CHECK(gear.update(c,2200,90,true,3200)==0);
    gear.reset();CHECK(gear.update(c,3000,90,true,0xffffff00u)==0);
    CHECK(gear.update(c,3000,90,true,0x200u)==3);
    c.ratios[1]=1.02f;CHECK(gear.update(c,3000,90,true,4000)==0);
    VehicleSnapshot snap{};snap.linkUp=true;snap.validMask=1u<<VF_SPEED;snap.valueMs[VF_SPEED]=100;
    CHECK(valueFresh(snap,VF_SPEED,2100));CHECK(!valueFresh(snap,VF_SPEED,2101));
    snap.linkUp=false;CHECK(!valueFresh(snap,VF_SPEED,100));
    PidScheduler scheduler;scheduler.begin();uint8_t idx=0,due[kPidCount];
    testMillis=1000;for(int i=0;i<5;++i)scheduler.reportFail(idx);
    CHECK(scheduler.isEnabled(idx));
    auto n=scheduler.selectDue(10999,due,kPidCount);bool found=false;for(int i=0;i<n;++i)found|=due[i]==idx;CHECK(!found);
    n=scheduler.selectDue(11000,due,kPidCount);found=false;for(int i=0;i<n;++i)found|=due[i]==idx;CHECK(found);
    scheduler.reportOk(idx);CHECK(scheduler.entry(idx).failCount==0);
    testMillis=0xfffffff0u;scheduler.markRequested(&idx,1,testMillis);
    n=scheduler.selectDue(0x20,due,kPidCount);found=false;for(int i=0;i<n;++i)found|=due[i]==idx;CHECK(!found);
    scheduler.begin();CHECK(scheduler.lastRequestMs(0)==0);
    // Slow single-PID adapter must eventually service every enabled PID.
    bool seen[kPidCount]{};
    for (int i=0;i<30;++i) {
        testMillis=1000+i*700;
        CHECK(scheduler.selectDue(testMillis,due,1)==1);
        seen[due[0]]=true;scheduler.markRequested(due,1,testMillis);scheduler.reportOk(due[0]);
    }
    for (size_t i=0;i<kPidCount;++i) if (scheduler.isEnabled(i)) CHECK(seen[i]);
    testMillis=0x90000000u;scheduler.begin();CHECK(scheduler.selectDue(testMillis,due,1)==1);
    LinkRecovery r;r.sessionStarted(0);
    CHECK(r.check(9000,true,true,false)==RecoveryAction::None);
    CHECK(r.check(9000,false,false,false)==RecoveryAction::None);
    CHECK(r.check(9000,true,false,true)==RecoveryAction::None);
    CHECK(r.check(9000,true,false,false)==RecoveryAction::Reinitialize);
    r.sessionStarted(10000);CHECK(r.check(18000,true,false,false)==RecoveryAction::Reinitialize);
    r.sessionStarted(19000);CHECK(r.check(27000,true,false,false)==RecoveryAction::Reconnect);
    r.sessionStarted(30000);r.observe(false,true,37000);CHECK(r.check(38000,true,false,false)==RecoveryAction::None);
    CHECK(r.check(48000,true,false,false)==RecoveryAction::None); // All PIDs cooling down is not adapter silence.
    r.observe(false,false,49000);
    CHECK(r.check(49000,true,false,false)==RecoveryAction::Reinitialize);
    for(int i=0;i<3;++i)r.observe(true,true,46000+i);
    r.observe(false,false,55000);
    CHECK(r.check(55000,true,false,false)==RecoveryAction::Reinitialize);
    r.sessionStarted(0xfffffff0u);CHECK(r.check(0x2000,true,false,false)==RecoveryAction::Reinitialize);
    testMillis=0;ElmClient e;Wire w;e.begin();ready(e,w);CHECK(e.caps().echoOff);
    scheduler.begin();CHECK(e.poll(w,scheduler,testMillis));CHECK(e.awaiting());
    w.connected_=false;e.tick(w,++testMillis);CHECK(!e.awaiting());CHECK(e.state()==ElmState::Idle);
    w.connected_=true;e.tick(w,++testMillis);ready(e,w);CHECK(e.poll(w,scheduler,testMillis));
    e.requestReinit();CHECK(!e.awaiting());ready(e,w);
    CHECK(e.pollRaw(w,"010C\r",testMillis));feed(e,"SEARCHING...");
    CHECK(!e.replyReady(testMillis+9000));CHECK(e.replyReady(testMillis+30001));
    feed(e,"\r410C0FA0\r>");ParsedPid out[8];CHECK(e.consumeReply(testMillis,scheduler,out,8)==1);CHECK(out[0].value==1000);
    CHECK(e.pollRaw(w,"010C\r",testMillis));feed(e,"410C0FA0");
    CHECK(e.consumeReply(testMillis+601,scheduler,out,8)==0);CHECK(!e.lastReplyHadPrompt());
    CHECK(e.pollRaw(w,"010C\r",testMillis));feed(e,"410CFFFF\r>");CHECK(e.consumeReply(testMillis,scheduler,out,8)==0);
    e.begin();ready(e,w,false);CHECK(!e.caps().echoOff);CHECK(e.caps().atOkCount==0);
    std::printf("PASS %d assertions (%s)\n",checks,profilePidEnabled(0xF0)?"bench":"vehicle");
}
