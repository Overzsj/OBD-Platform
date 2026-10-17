#pragma once
#include <cmath>
#include <stdint.h>
namespace obd {
// Presentation only: never feeds telemetry, thresholds or numeric labels.
class BarMotion {
public:
 float update(float target,bool good,uint32_t now){
  if(!good||!std::isfinite(target)){valid_=false;height_=0;last_=now;return 0;}
  target=target<0?0:target>49?49:target;
  if(!valid_){height_=target;valid_=true;}
  else {const uint32_t dt=uint32_t(now-last_);height_+=(target-height_)*(-std::expm1(-float(dt)/180.0f));
   if(std::fabs(target-height_)<0.025f)height_=target;}
  last_=now;return height_;
 }
 float height()const{return height_;}
private:
 float height_=0;uint32_t last_=0;bool valid_=false;
};
}
