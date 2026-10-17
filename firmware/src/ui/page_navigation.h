#pragma once
#include <stdint.h>
#include <string.h>
#include "../app/build_mode.h"
#ifndef OBD_TEST_PAGE_CYCLE_MS
#define OBD_TEST_PAGE_CYCLE_MS 0
#endif
namespace obd {
class PageCycleTimer {
    uint32_t last_=0;bool started_=false;
public:
    bool tick(uint32_t now,bool blocked,uint32_t interval) {
        if(blocked || !interval){started_=false;return false;}
        if(!started_){last_=now;started_=true;return false;}
        if(uint32_t(now-last_)<interval)return false;
        last_=now;return true;
    }
};
enum class DisplayPage:uint8_t { Main=0,Data=1,Diagnostics=2 };
constexpr uint8_t kDisplayPages=3;
inline const char* pageName(DisplayPage page) {
    return page==DisplayPage::Main?"MAIN":page==DisplayPage::Data?"DATA":"DIAG";
}
inline DisplayPage adjacentPage(DisplayPage current,bool next) {
    return DisplayPage((uint8_t(current)+(next?1:2))%kDisplayPages);
}
inline bool parsePageRequest(const char* op,DisplayPage current,DisplayPage& out) {
    DisplayPage next=current;
    if(!strcmp(op,"VIEW MAIN") || !strcmp(op,"PAGE 0"))next=DisplayPage::Main;
    else if(!strcmp(op,"VIEW DATA"))next=DisplayPage::Data;
    else if(!strcmp(op,"VIEW DIAG") || !strcmp(op,"PAGE 1"))next=DisplayPage::Diagnostics;
    else if(!strcmp(op,"VIEW NEXT"))next=adjacentPage(current,true);
    else if(!strcmp(op,"VIEW PREV"))next=adjacentPage(current,false);
    else return false;
    out=next;return true;
}
}
