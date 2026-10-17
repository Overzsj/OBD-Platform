#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
namespace obd {
enum class AdapterMode:uint8_t {Bt=0,Ble=1,Wifi=2};
inline const char* adapterModeName(AdapterMode m){return m==AdapterMode::Ble?"BLE":m==AdapterMode::Wifi?"WIFI":"BT";}
struct AdapterConfig {
    uint32_t version=1;
    AdapterMode mode=AdapterMode::Bt;
    char ssid[33]{},password[64]{},host[16]="192.168.0.10",mac[18]{},pin[17]{};
    char service[37]{},rx[37]{},tx[37]{};
    uint16_t port=35000;
    uint32_t checksum=0;
};
inline uint32_t adapterHash(const AdapterConfig& c){const auto* p=reinterpret_cast<const uint8_t*>(&c);uint32_t h=2166136261u;for(size_t i=0;i<offsetof(AdapterConfig,checksum);++i)h=(h^p[i])*16777619u;return h;}
inline int hexDigit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
inline bool adapterUuid(const char* s){if(!*s)return true;if(strlen(s)!=36)return false;for(int i=0;i<36;++i)if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}else if(hexDigit(s[i])<0)return false;return true;}
inline bool adapterMac(const char* s){if(!*s)return true;if(strlen(s)!=17)return false;for(int i=0;i<17;++i)if(i%3==2){if(s[i]!=':')return false;}else if(hexDigit(s[i])<0)return false;return true;}
inline bool adapterIp(const char* s){unsigned parts=0;while(*s){unsigned n=0,digits=0;while(*s>='0'&&*s<='9'){n=n*10+*s++-'0';if(++digits>3||n>255)return false;}if(!digits)return false;++parts;if(!*s)break;if(*s++!='.'||!*s)return false;}return parts==4;}
inline bool validAdapter(const AdapterConfig& c){
    if(c.version!=1 || uint8_t(c.mode)>2 || !c.port)return false;
    if(!memchr(c.ssid,0,sizeof(c.ssid))||!memchr(c.password,0,sizeof(c.password))||!memchr(c.host,0,sizeof(c.host))||!memchr(c.mac,0,sizeof(c.mac))||!memchr(c.pin,0,sizeof(c.pin))||!memchr(c.service,0,sizeof(c.service))||!memchr(c.rx,0,sizeof(c.rx))||!memchr(c.tx,0,sizeof(c.tx)))return false;
    if(!adapterIp(c.host)||!adapterMac(c.mac)||!adapterUuid(c.service)||!adapterUuid(c.rx)||!adapterUuid(c.tx))return false;
    for(auto p=c.pin;*p;++p)if(*p<'0'||*p>'9')return false;
    const size_t pw=strlen(c.password);return (pw==0||pw>=8)&&pw<=63;
}
// Values are UTF-8 hex so spaces and credentials cannot inject USB commands.
// A failed field update never mutates the caller's configuration.
inline bool adapterField(AdapterConfig& config,const char* field,const char* hex){
    char value[128]{};const size_t n=strlen(hex);if(strcmp(hex,"-")&&(n%2||n/2>=sizeof(value)))return false;
    if(strcmp(hex,"-")){for(size_t i=0;i<n;i+=2){int a=hexDigit(hex[i]),b=hexDigit(hex[i+1]);if(a<0||b<0)return false;value[i/2]=char(a*16+b);if(uint8_t(value[i/2])<32||uint8_t(value[i/2])==127)return false;}}
    AdapterConfig next=config;char* dest=nullptr;size_t cap=0;
    if(!strcmp(field,"MODE")){if(!strcmp(value,"BT"))next.mode=AdapterMode::Bt;else if(!strcmp(value,"BLE"))next.mode=AdapterMode::Ble;else if(!strcmp(value,"WIFI"))next.mode=AdapterMode::Wifi;else return false;}
    else if(!strcmp(field,"PORT")){unsigned long number=0;if(!*value)return false;for(auto p=value;*p;++p){if(*p<'0'||*p>'9')return false;number=number*10+*p-'0';if(number>65535)return false;}next.port=number;}
    else {
        if(!strcmp(field,"SSID")){dest=next.ssid;cap=sizeof(next.ssid);}else if(!strcmp(field,"PASSWORD")){dest=next.password;cap=sizeof(next.password);}
        else if(!strcmp(field,"HOST")){dest=next.host;cap=sizeof(next.host);}else if(!strcmp(field,"MAC")){dest=next.mac;cap=sizeof(next.mac);}
        else if(!strcmp(field,"PIN")){dest=next.pin;cap=sizeof(next.pin);}else if(!strcmp(field,"SERVICE")){dest=next.service;cap=sizeof(next.service);}
        else if(!strcmp(field,"RX")){dest=next.rx;cap=sizeof(next.rx);}else if(!strcmp(field,"TX")){dest=next.tx;cap=sizeof(next.tx);}else return false;
        if(strlen(value)>=cap)return false;memset(dest,0,cap);strcpy(dest,value);
    }
    if(!validAdapter(next))return false;config=next;return true;
}
}
