#include <cstdio>
#include <cmath>
#include "ui/bar_motion.h"
using namespace obd;
#define CHECK(x) do{if(!(x)){printf("FAIL %d\n",__LINE__);return 1;}}while(0)
int main(){
 BarMotion a,b;CHECK(a.update(0,true,0)==0);b.update(0,true,0);float previous=0;
 for(unsigned t=33;t<=990;t+=33){float h=a.update(49,true,t);CHECK(h>=previous&&h<=49);previous=h;}
 CHECK(std::fabs(b.update(49,true,990)-a.height())<.03f);CHECK(a.height()>48.7f);
 CHECK(a.update(49,false,1000)==0);CHECK(a.update(20,true,1033)==20);
 CHECK(a.update(NAN,true,1066)==0);CHECK(a.update(-20,true,1100)==0);CHECK(a.update(100,true,1133)<49);
 BarMotion wrap;wrap.update(0,true,0xfffffff0u);CHECK(wrap.update(49,true,17)>0);CHECK(wrap.height()<10);
 previous=a.height();for(unsigned t=1200;t<3000;t+=33){float h=a.update(0,true,t);CHECK(h>=0&&h<=previous);previous=h;}
 puts("PASS bars: monotonic rise/fall, frame-rate independence, invalidation, reacquisition, clamp, clock wrap");
}
