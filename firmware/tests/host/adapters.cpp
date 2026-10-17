#include <cstdio>
#include <cstdlib>
#include <string>
#include "bt/adapter_config.h"
#include "bt/adapter_policy.h"
using namespace obd;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
std::string hex(const std::string& s){std::string out;const char* digits="0123456789abcdef";for(unsigned char c:s){out+=digits[c>>4];out+=digits[c&15];}return out.empty()?"-":out;}
bool set(AdapterConfig& c,const char* field,const std::string& value){auto encoded=hex(value);return adapterField(c,field,encoded.c_str());}
int main(){
    AdapterConfig c{};CHECK(validAdapter(c));CHECK(c.mode==AdapterMode::Bt);CHECK(adapterIp(c.host));
    CHECK(set(c,"MODE","BLE"));CHECK(c.mode==AdapterMode::Ble);CHECK(set(c,"MODE","WIFI"));
    CHECK(set(c,"SSID","OBD adapter 网络"));CHECK(set(c,"PASSWORD","test pass123"));CHECK(set(c,"PASSWORD",""));CHECK(!*c.password);
    CHECK(set(c,"MAC","AA:bb:12:34:56:78"));CHECK(set(c,"PIN","1234"));CHECK(set(c,"PIN",""));
    CHECK(set(c,"PORT","65535"));CHECK(c.port==65535);
    CHECK(set(c,"SERVICE",adapters::ble[0].service));CHECK(set(c,"TX",adapters::ble[0].tx));CHECK(set(c,"RX",adapters::ble[0].rx));
    const auto before=c;
    const char* invalidModes[]={"USB","MFI","UART","AUTO","ble"};
    for(auto mode:invalidModes){CHECK(!set(c,"MODE",mode));CHECK(!memcmp(&c,&before,sizeof(c)));}
    struct Bad {const char* field;const char* value;};
    const Bad bad[]={{"PORT","0"},{"PORT","65536"},{"PORT","-1"},{"PORT","35x"},{"HOST","1.2.3.256"},{"HOST","1.2.3."},{"HOST","0.0.0"},{"HOST",""},{"MAC","aa:bb:cc:dd:ee:gg"},{"PASSWORD","short"},{"PIN","12x4"},{"RX","FFF1"},{"RX","0000fff1-0000-1000-8000-00805f9b34fz"},{"UNKNOWN","x"},{"SSID","line\nbreak"}};
    for(auto b:bad){CHECK(!set(c,b.field,b.value));CHECK(!memcmp(&c,&before,sizeof(c)));}
    CHECK(!set(c,"SSID",std::string(33,'x')));CHECK(!set(c,"PASSWORD",std::string(64,'x')));
    CHECK(!adapterField(c,"SSID","0"));CHECK(!adapterField(c,"SSID","zz"));CHECK(!adapterField(c,"SSID","00"));CHECK(!memcmp(&c,&before,sizeof(c)));
    c.checksum=adapterHash(c);CHECK(c.checksum==adapterHash(c));c.port--;CHECK(c.checksum!=adapterHash(c));
    CHECK(adapterNameAllowed("Veepeak OBDCheck"));CHECK(adapterNameAllowed("Viecar"));CHECK(adapterNameAllowed("OBD II"));
    CHECK(!adapterNameAllowed("Scan Tool Pro"));CHECK(!adapterNameAllowed("Micro Mechanic OBD"));CHECK(!adapterNameAllowed("THINMI.COM OBDII"));CHECK(!adapterNameAllowed("headphones"));
    CHECK(adapterNameAllowed("OBD BLE"));CHECK(!adapterNameAllowed("LED Controller FFF0"));
    for(auto& def:adapters::ble){CHECK(adapterUuid(def.service));CHECK(adapterUuid(def.tx));CHECK(adapterUuid(def.rx));CHECK(strcmp(def.tx,def.rx));}
    // Arbitrary input must never partially mutate configuration on rejection.
    unsigned seed=17;for(int n=0;n<500;++n){char input[80]{};for(int i=0;i<n%79;++i){seed=seed*1664525u+1013904223u;input[i]=32+seed%95;}AdapterConfig copy=before;bool ok=adapterField(copy,"SSID",input);CHECK(ok?validAdapter(copy):!memcmp(&copy,&before,sizeof(copy)));}
    printf("PASS %u adapter assertions: strict config, source definitions, exclusions, rejected updates atomic\n",checks);
}
