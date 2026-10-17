#pragma once
#include <cmath>
#include <stddef.h>
namespace obd {
struct RpmCurveScale {
    float low,high;
    static bool valid(float rpm){return std::isfinite(rpm) && rpm>=0;}
    static RpmCurveScale from(const float* samples,size_t count){
        float lo=10000,hi=0;bool found=false;
        for(size_t i=0;i<count;++i)if(valid(samples[i])){float v=samples[i]>10000?10000:samples[i];lo=v<lo?v:lo;hi=v>hi?v:hi;found=true;}
        if(!found)return {0,2000};
        // Minimum 1500 RPM span prevents tiny idle noise filling the plot.
        float lower=std::floor((lo-150)/500)*500;
        if(lower<0)lower=0;
        float upper=std::ceil((hi+150)/500)*500;
        if(upper-lower<1500){lower=std::floor(((lo+hi)*.5f-750)/500)*500;if(lower<0)lower=0;upper=lower+2000;}
        return {lower,upper};
    }
    int y(float rpm) const {
        float ratio=(rpm-low)/(high-low);if(ratio<0)ratio=0;if(ratio>1)ratio=1;
        return 207-int(std::lround(ratio*21));
    }
};
}
