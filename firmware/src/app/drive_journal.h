#pragma once
#include <SPIFFS.h>
#include <Preferences.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <errno.h>
#ifdef ARDUINO
#include <unistd.h>
#include <esp_spiffs.h>
#endif
#include "drive_record.h"
namespace obd {
class DriveJournal {
public:
    static constexpr uint32_t kIntervalMs=1000,kFlushMs=5000,kSegmentMs=60000;
    bool enabled=!kSimulatedSource,available=false;const char* error="NOT_STARTED";
    uint32_t nextSeq=1,dropped=0;uint8_t pending=0;
    bool due(uint32_t now)const{return enabled&&available&&!kSimulatedSource&&uint32_t(now-lastCheck_)>=250;}
    void begin(){
        prefs_.begin("drivejournal",false);enabled=!kSimulatedSource&&prefs_.getBool("vehicle",true);
        if(!SPIFFS.begin(false,"/spiffs",2)){
            const auto* part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"spiffs");
            if(!part){error="NO_PARTITION";return;}
            uint8_t bytes[512];bool blank=true;
            for(size_t offset=0;offset<part->size&&blank;offset+=sizeof(bytes)){
                if(esp_partition_read(part,offset,bytes,sizeof(bytes))!=ESP_OK){blank=false;break;}
                for(auto byte:bytes)if(byte!=0xff){blank=false;break;}
            }
            if(!blank){error="NONBLANK_UNMOUNTABLE_NOT_FORMATTED";return;}
            if(!SPIFFS.format()||!SPIFFS.begin(false,"/spiffs",2)){error="FS_INIT_FAILED";return;}
        }
        uint32_t newest=0;
        for(uint8_t i=0;i<2;++i){
            char name[32];path(i,name);File f=SPIFFS.open(name,FILE_READ);if(!f)continue;
            DriveRecordV2 r;uint32_t count=0,first=0,last=0;
            while(f.read(reinterpret_cast<uint8_t*>(&r),sizeof(r))==sizeof(r)&&r.validV2()){
                if(!count)first=r.seq;last=r.seq;++count;
            }
            if(count){index_[i]={first,last,count};if(last>newest){newest=last;current_=i;}}
            f.close();
        }
        nextSeq=newest+1;available=true;error="OK";lastFlush_=millis();
    }
    void tick(const VehicleSnapshot& s,uint32_t boot,uint32_t now,uint32_t heap,uint8_t mode,const char* state){
        if(kSimulatedSource||!enabled||!available||uint32_t(now-lastCheck_)<250)return;
        lastCheck_=now;
        if(!started_){
            // Each new boot starts a new segment; never append across time bases.
            if(index_[current_].count){if(!rotate())return;}
            else { // An interrupted first write must never become an unreadable prefix.
                char name[32];path(current_,name);
                if(SPIFFS.exists(name)&&!SPIFFS.remove(name)){fail("RECOVERY_FAILED");return;}
            }
            segmentStart_=now;started_=true;
        }else if(uint32_t(now-segmentStart_)>=kSegmentMs){
            if(!flush()||!rotate())return;
            segmentStart_=now;
        }
        DriveRecordV2 r;static_cast<DriveRecord&>(r)=driveSample(s,boot,now,heap,mode,driveAdapterState(state));
        const uint8_t ids[]={1,3,2};for(uint8_t i=0;i<3;++i)if(candidateFresh(s,ids[i],now)){r.candidates[i]=s.candidateValues[ids[i]];r.candidateMask|=1u<<i;}
        const uint8_t reason=sampled_?driveReason(previous_,r):1;
        if(!sampled_||reason||uint32_t(now-lastSample_)>=kIntervalMs){
            r.seq=nextSeq++;r.reason=reason|(!sampled_?uint8_t(esp_reset_reason())<<4:0);r.sealV2();
            buffer_[pending++]=r;previous_=r;sampled_=true;lastSample_=now;
        }
        if(pending==16||uint32_t(now-lastFlush_)>=kFlushMs)flush();
    }
    bool flush(){
        if(!available)return false;
        if(kSimulatedSource)return true;
        if(pending){
            char name[32];path(current_,name);
            if(!appendVerified(name,buffer_,pending)){fail("WRITE_NOT_COMMITTED");return false;}
            if(!index_[current_].count)index_[current_].first=buffer_[0].seq;
            index_[current_].last=buffer_[pending-1].seq;index_[current_].count+=pending;pending=0;
        }
        lastFlush_=millis();return true;
    }
    bool setEnabled(bool on){
        if(kSimulatedSource){enabled=false;return !on;}
        if(!on&&!flush()&&available)return false;
        if(prefs_.putBool("vehicle",on)!=1)return false;enabled=on;return true;
    }
    bool clearLogs(){
        // Explicit user maintenance only: known journal files, never format or touch settings.
        for(uint8_t version=1;version<=2;++version)for(uint8_t mode=0;mode<2;++mode)for(uint8_t slot=0;slot<4;++slot){
            char name[32];snprintf(name,sizeof(name),"/%s-v%u-%u.bin",mode?"bench":"vehicle",version,slot);
            if(SPIFFS.exists(name)&&!SPIFFS.remove(name))return false;
        }
        for(uint8_t slot=0;slot<2;++slot){char name[32];path(slot,name);if(SPIFFS.exists(name)&&!SPIFFS.remove(name))return false;}
        for(auto& i:index_)i=Index{};pending=0;nextSeq=1;dropped=0;current_=0;started_=sampled_=false;
        lastSample_=lastCheck_=lastFlush_=millis();error="OK";return true;
    }
    uint32_t records()const{uint32_t n=pending;for(const auto& i:index_)n+=i.count;return n;}
    uint32_t oldest()const{uint32_t n=nextSeq;for(const auto& i:index_)if(i.count&&i.first<n)n=i.first;if(pending&&buffer_[0].seq<n)n=buffer_[0].seq;return n==nextSeq?0:n;}
    template<class Emit> uint32_t read(uint32_t after,uint8_t limit,Emit emit){
        if(!flush())return 0;uint32_t sent=0;uint8_t order[2]={0,1};
        if(index_[0].first>index_[1].first){order[0]=1;order[1]=0;}
        for(auto slot:order){
            if(!index_[slot].count||index_[slot].last<=after)continue;
            char name[32];path(slot,name);File f=SPIFFS.open(name,FILE_READ);if(!f)continue;
            DriveRecordV2 r;
            while(sent<limit&&f.read(reinterpret_cast<uint8_t*>(&r),sizeof(r))==sizeof(r))if(r.validV2()&&r.seq>after)emit(r,sent++);
            f.close();if(sent==limit)break;
        }return sent;
    }
private:
    struct Index{uint32_t first,last,count;Index(uint32_t a=0,uint32_t b=0,uint32_t c=0):first(a),last(b),count(c){}}index_[2]{};
    DriveRecordV2 buffer_[16]{};DriveRecord previous_{};Preferences prefs_;
    uint32_t lastSample_=0,lastCheck_=0,lastFlush_=0,segmentStart_=0;uint8_t current_=0;bool started_=false,sampled_=false;
    void path(uint8_t slot,char* out)const{snprintf(out,32,"/vehicle-60s-%u.bin",unsigned(slot));}
    bool rotate(){
        current_^=1;char name[32];path(current_,name);
        if(SPIFFS.exists(name)&&!SPIFFS.remove(name)){fail("ROTATE_FAILED");return false;}
        index_[current_]={};return true;
    }
    bool appendVerified(const char* name,const DriveRecordV2* rows,uint32_t count){
        const size_t bytes=count*sizeof(DriveRecordV2);
#ifdef ARDUINO
        char absolute[48];snprintf(absolute,sizeof(absolute),"/spiffs%s",name);
        FILE* f=fopen(absolute,"ab");
        if(!f){Serial.printf("JOURNAL_IO|open|%s|errno=%d\n",name,errno);return false;}
        setvbuf(f,nullptr,_IONBF,0);if(fseek(f,0,SEEK_END)){fclose(f);return false;}const long before=ftell(f);
        errno=0;const size_t written=fwrite(rows,1,bytes,f);const int code=errno;
        const int flushed=fflush(f);const int flushCode=errno;const int closed=fclose(f);
        // ESP32 SPIFFS has no fsync implementation. fflush + reopen/readback is the commit check.
        if(written!=bytes||flushed||closed){Serial.printf("JOURNAL_IO|commit|%s|bytes=%u/%u|flush=%d|close=%d|write_errno=%d|flush_errno=%d\n",name,unsigned(written),unsigned(bytes),flushed,closed,code,flushCode);return false;}
#else
        File f=SPIFFS.open(name,FILE_APPEND);if(!f)return false;const size_t before=f.size();
        const size_t written=f.write(reinterpret_cast<const uint8_t*>(rows),bytes);f.close();if(written!=bytes)return false;
#endif
        File check=SPIFFS.open(name,FILE_READ);if(!check||before<0||check.size()!=size_t(before)+bytes){check.close();return false;}
        check.seek(before);DriveRecordV2 record;
        for(uint32_t i=0;i<count;++i)if(check.read(reinterpret_cast<uint8_t*>(&record),sizeof(record))!=sizeof(record)||!record.validV2()||memcmp(&record,rows+i,sizeof(record))){check.close();return false;}
        check.close();return true;
    }
    void fail(const char* why){error=why;available=false;dropped+=pending;pending=0;}
};
}
