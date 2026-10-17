#pragma once
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>
#include <atomic>
#include "bt_spp.h"
#include "adapter_store.h"
#include "adapter_policy.h"
namespace obd {
// One active radio transport per boot. Configuration changes require restart.
class AdapterTransport {
public:
    AdapterStore store;
    bool begin(const char* name="OBD-Gauge") {
        store.begin();active_=store.config;
        if(active_.mode==AdapterMode::Bt){bt_.target(active_.mac,active_.pin);return bt_.begin(name);}
        if(active_.mode==AdapterMode::Wifi){WiFi.persistent(false);WiFi.mode(WIFI_STA);tcp_.setTimeout(200);state_=*active_.ssid?"WIFI_IDLE":"WIFI_NEEDS_SSID";return true;}
        BLEDevice::init(name);client_=BLEDevice::createClient();
        if(!client_){state_="BLE_NO_MEMORY";return false;}
        scan_=BLEDevice::getScan();scanCallback_.owner=this;scan_->setAdvertisedDeviceCallbacks(&scanCallback_);scan_->setActiveScan(true);scan_->setInterval(120);scan_->setWindow(60);state_="BLE_IDLE";return true;
    }
    bool supported() const{return true;}
    AdapterMode mode() const{return active_.mode;}
    bool restartRequired() const{return memcmp(&active_,&store.config,offsetof(AdapterConfig,checksum))!=0;}
    void tick(uint32_t now){
        if(active_.mode==AdapterMode::Bt){bt_.tick(now);return;}
        if(active_.mode==AdapterMode::Wifi){tickWifi(now);return;}
        if(!client_||!scan_)return;
        if(client_->isConnected()&&tx_&&rx_){
            if(overflow_.load()){disconnectBle();state_="BLE_RX_OVERFLOW";lastAttempt_=now;}return;
        }
        if(tx_||rx_){disconnectBle();state_="BLE_LOST";lastAttempt_=now;}
        if(scanning_){if(uint32_t(now-scanStarted_)<3300)return;scanning_=false;scan_->stop();
            if(found_.load()){connectBle();lastAttempt_=millis();return;}state_="BLE_NOT_FOUND";lastAttempt_=now;return;}
        if(lastAttempt_&&uint32_t(now-lastAttempt_)<8000)return;
        found_.store(false);scan_->clearResults();state_="BLE_SCANNING";scanStarted_=now;
        scanning_=scan_->start(3,[](BLEScanResults){},false);
        if(!scanning_){state_="BLE_SCAN_FAILED";lastAttempt_=now;}
    }
    bool isConnected(){if(active_.mode==AdapterMode::Bt)return bt_.isConnected();if(active_.mode==AdapterMode::Wifi)return tcp_.connected();return client_&&client_->isConnected()&&tx_&&rx_&&!overflow_.load();}
    int available(){if(active_.mode==AdapterMode::Bt)return bt_.available();if(active_.mode==AdapterMode::Wifi)return tcp_.available();if(overflow_.load())return 0;portENTER_CRITICAL(&mux_);int count=(head_+sizeof(ring_)-tail_)%sizeof(ring_);portEXIT_CRITICAL(&mux_);return count;}
    int read(){if(active_.mode==AdapterMode::Bt)return bt_.read();if(active_.mode==AdapterMode::Wifi)return tcp_.read();if(overflow_.load())return -1;portENTER_CRITICAL(&mux_);int c=-1;if(head_!=tail_){c=ring_[tail_];tail_=(tail_+1)%sizeof(ring_);}portEXIT_CRITICAL(&mux_);return c;}
    size_t write(const char* data,size_t n){
        if(active_.mode==AdapterMode::Bt)return bt_.write(data,n);
        if(!isConnected())return 0;
        if(active_.mode==AdapterMode::Wifi)return tcp_.write(reinterpret_cast<const uint8_t*>(data),n);
        size_t sent=0;while(sent<n&&isConnected()){size_t count=n-sent>20?20:n-sent;tx_->writeValue((uint8_t*)data+sent,count,tx_->canWrite());sent+=count;if(sent<n)delay(20);}return sent;
    }
    String describeState() const {if(active_.mode==AdapterMode::Bt)return String("BT/")+bt_.describeState();return state_;}
    void requestReconnect(uint32_t now){if(active_.mode==AdapterMode::Bt){bt_.requestReconnect(now);return;}if(active_.mode==AdapterMode::Wifi){tcp_.stop();WiFi.disconnect();state_="WIFI_IDLE";}else{if(scanning_){scan_->stop();scanning_=false;}disconnectBle();state_="BLE_IDLE";}lastAttempt_=0;}
    void forgetMac(){if(active_.mode==AdapterMode::Bt)bt_.forgetMac();}
private:
    struct ScanCallback:BLEAdvertisedDeviceCallbacks {
        AdapterTransport* owner=nullptr;
        void onResult(BLEAdvertisedDevice device) override {
            if(!owner||owner->found_.load())return;
            const std::string mac=device.getAddress().toString();bool match=false;
            if(*owner->active_.mac)match=String(mac.c_str()).equalsIgnoreCase(owner->active_.mac);
            else {
                // FFF0 is shared by non-OBD products. Auto-connect requires an
                // adapter name; unnamed/custom devices require an explicit MAC.
                if(device.haveName()&&adapterNameAllowed(device.getName().c_str()))match=true;
            }
            if(!match)return;
            owner->foundMac_=mac.c_str();owner->foundType_=device.getAddressType();owner->found_.store(true);
        }
    } scanCallback_;
    void disconnectBle(){tx_=rx_=nullptr;if(client_&&client_->isConnected())client_->disconnect();portENTER_CRITICAL(&mux_);head_=tail_=0;portEXIT_CRITICAL(&mux_);overflow_.store(false);}
    bool endpoints(const char* service,const char* tx,const char* rx){
        if(!*service||!*tx||!*rx)return false;
        auto* s=client_->getService(service);if(!s)return false;
        auto* output=s->getCharacteristic(BLEUUID(tx));auto* input=s->getCharacteristic(BLEUUID(rx));
        if(!output||!input||(!output->canWrite()&&!output->canWriteNoResponse())||(!input->canNotify()&&!input->canIndicate()))return false;
        tx_=output;rx_=input;
        rx_->registerForNotify([this](BLERemoteCharacteristic*,uint8_t* data,size_t n,bool){
            portENTER_CRITICAL(&mux_);for(size_t i=0;i<n;++i){const size_t next=(head_+1)%sizeof(ring_);if(next==tail_){overflow_.store(true);break;}ring_[head_]=data[i];head_=next;}portEXIT_CRITICAL(&mux_);
        },input->canNotify());
        return client_->isConnected();
    }
    void connectBle(){
        state_="BLE_CONNECTING";disconnectBle();
        if(!client_->connect(BLEAddress(foundMac_.c_str()),foundType_)){state_="BLE_CONNECT_FAILED";return;}
        // Discovery checks properties; extracted Input/Output names are never guessed.
        if(*active_.service){if(endpoints(active_.service,active_.tx,active_.rx)){state_="BLE_CONNECTED";return;}}
        else for(auto& def:adapters::ble)if(endpoints(def.service,def.tx,def.rx)){state_="BLE_CONNECTED";return;}
        disconnectBle();state_="BLE_NO_ELM_ENDPOINTS";
    }
    void tickWifi(uint32_t now){
        if(!*active_.ssid){state_="WIFI_NEEDS_SSID";return;}
        if(WiFi.status()!=WL_CONNECTED){tcp_.stop();if(!lastAttempt_||uint32_t(now-lastAttempt_)>=15000){WiFi.begin(active_.ssid,active_.password);lastAttempt_=now;}state_="WIFI_JOINING";return;}
        if(tcp_.connected()){state_="WIFI_CONNECTED";return;}
        if(lastTcp_&&uint32_t(now-lastTcp_)<5000)return;lastTcp_=now;IPAddress ip;ip.fromString(active_.host);
        if(tcp_.connect(ip,active_.port,1000)){tcp_.setNoDelay(true);state_="WIFI_CONNECTED";}else state_="WIFI_TCP_FAILED";
    }
    AdapterConfig active_{};
    BtSpp bt_;WiFiClient tcp_;BLEClient* client_=nullptr;BLEScan* scan_=nullptr;
    BLERemoteCharacteristic *tx_=nullptr,*rx_=nullptr;
    const char* state_="IDLE";
    uint32_t lastAttempt_=0,lastTcp_=0,scanStarted_=0;
    bool scanning_=false;std::atomic<bool> found_{false},overflow_{false};
    String foundMac_;esp_ble_addr_type_t foundType_=BLE_ADDR_TYPE_PUBLIC;
    uint8_t ring_[1024]{};size_t head_=0,tail_=0;portMUX_TYPE mux_=portMUX_INITIALIZER_UNLOCKED;
};
}
