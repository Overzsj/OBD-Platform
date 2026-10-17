#pragma once
#include <Preferences.h>
#include "alarm_events.h"
#include "display_settings.h"
namespace obd {
// Owned by acquisition task after setup. Never accessed from UI callbacks.
class DeviceStorage {
public:
    DeviceSettings settings{};
    PresentationOptions presentation{};
    DisplaySettings display{};
    AlarmEvents events;
    uint32_t boot=0,lastFlush=0;
    bool available=false,settingsRecovered=false,writeFailed=false;
    void begin() {
        available=prefs.begin(OBD_DRIVE_REPLAY?"gaugereplay":OBD_DATA_TEST?"gauge2test":"gauge2",false);
        if(!available)return;
        DisplayRecord displayRecord;
        if(prefs.getBytesLength("display")==sizeof(displayRecord)&&prefs.getBytes("display",&displayRecord,sizeof(displayRecord))==sizeof(displayRecord)&&displayRecord.valid())display=displayRecord.value;
        const uint32_t options=prefs.getUInt("presentation",0xA5000700);
        PresentationOptions decoded;decoded.layout=options&255;decoded.sounds=(options>>8)&255;
        if((options&0xffff0000)==0xA5000000&&decoded.valid())presentation=decoded;
        SettingsRecord record;
        const size_t bytes=prefs.getBytesLength("settings");
        if(bytes) {
            if(bytes==sizeof(record) && prefs.getBytes("settings",&record,sizeof(record))==sizeof(record) && record.valid())settings=record.value;
            else settingsRecovered=true;
        }
        EventRecord saved;
        if(prefs.getBytesLength("events")==sizeof(saved) && prefs.getBytes("events",&saved,sizeof(saved))==sizeof(saved) && saved.valid())events.history=saved;
        boot=prefs.getUInt("boot",0)+1;
        writeFailed=prefs.putUInt("boot",boot)!=sizeof(uint32_t);
        lastFlush=millis();
    }
    bool save(const DeviceSettings& next) {
        if(!available || !validSettings(next))return false;
        if(!settingsRecovered && memcmp(&next,&settings,sizeof(next))==0)return true;
        SettingsRecord r;r.value=next;r.seal();
        if(prefs.putBytes("settings",&r,sizeof(r))!=sizeof(r)) {writeFailed=true;return false;}
        settings=next;settingsRecovered=false;writeFailed=false;return true;
    }
    bool savePresentation(const PresentationOptions& next){
        if(!available||!next.valid())return false;
        const uint32_t packed=0xA5000000u|(uint32_t(next.sounds)<<8)|next.layout;
        if(prefs.putUInt("presentation",packed)!=sizeof(uint32_t))return false;
        presentation=next;return true;
    }
    bool saveDisplay(const DisplaySettings& next){
        if(!available||!next.valid())return false;
        if(!memcmp(&display,&next,sizeof(next)))return true;
        DisplayRecord r;r.value=next;r.seal();
        if(prefs.putBytes("display",&r,sizeof(r))!=sizeof(r))return false;
        display=next;return true;
    }
    void tick(uint32_t now) {
        if(!available || !events.dirty || uint32_t(now-lastFlush)<60000)return;
        lastFlush=now;events.history.seal();
        writeFailed=prefs.putBytes("events",&events.history,sizeof(events.history))!=sizeof(events.history);
        if(!writeFailed)events.dirty=false;
    }
private:
    Preferences prefs;
};
}
