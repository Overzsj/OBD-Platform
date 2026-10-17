#pragma once
#include <Preferences.h>
#include "adapter_config.h"
namespace obd {
class AdapterStore {
public:
    AdapterConfig config{};bool available=false,recovered=false;
    void begin(){available=prefs_.begin("adapter_b",false);if(!available)return;AdapterConfig stored{};
        if(prefs_.getBytesLength("config")){if(prefs_.getBytesLength("config")==sizeof(stored)&&prefs_.getBytes("config",&stored,sizeof(stored))==sizeof(stored)&&validAdapter(stored)&&stored.checksum==adapterHash(stored))config=stored;else recovered=true;}}
    bool field(const char* key,const char* value){AdapterConfig next=config;if(!adapterField(next,key,value)||!available)return false;next.checksum=adapterHash(next);if(prefs_.putBytes("config",&next,sizeof(next))!=sizeof(next))return false;config=next;recovered=false;return true;}
private:Preferences prefs_;
};
}
