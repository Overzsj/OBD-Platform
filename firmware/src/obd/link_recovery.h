#pragma once
#include <stdint.h>
namespace obd {
enum class RecoveryAction : uint8_t { None, Reinitialize, Reconnect };
class LinkRecovery {
public:
    // Independent of prompt completion and polling state. AT timeouts can reach
    // Ready even on a stale SPP session; they must not renew this RX deadline.
    void transportDisconnected(){rxActive_=false;}
    RecoveryAction checkTransport(uint32_t now,bool ready,uint32_t bytesIn,bool searching) {
        if(!rxActive_ || bytesIn!=rxBytes_){rxActive_=true;rxBytes_=bytesIn;lastRx_=now;return RecoveryAction::None;}
        const uint32_t limit=ready&&!searching?8000:45000;
        if(uint32_t(now-lastRx_)<limit)return RecoveryAction::None;
        rxActive_=false;
        return RecoveryAction::Reconnect;
    }
    void sessionStarted(uint32_t now) { lastResponse_=now; good_=0; silent_=true; }
    void observe(bool validData,bool completePrompt,uint32_t now) {
        // NO DATA/CAN ERROR with a prompt means the adapter is alive.
        if (completePrompt) lastResponse_=now;
        silent_=!completePrompt;
        if (validData && completePrompt) {
            if (++good_>=3) { failures_=0;good_=3; }
        } else good_=0;
    }
    RecoveryAction check(uint32_t now,bool ready,bool awaiting,bool scanning) {
        // Never interrupt AT initialization, a 30s SEARCHING request or a scan.
        if (!ready || awaiting || scanning || !silent_ || uint32_t(now-lastResponse_)<8000) return RecoveryAction::None;
        lastResponse_=now;good_=0;
        if (++failures_>=3) { failures_=0;return RecoveryAction::Reconnect; }
        return RecoveryAction::Reinitialize;
    }
private:
    uint32_t lastRx_=0,rxBytes_=0;bool rxActive_=false;
    uint32_t lastResponse_=0;
    uint8_t failures_=0,good_=0;
    bool silent_=true;
};
}
