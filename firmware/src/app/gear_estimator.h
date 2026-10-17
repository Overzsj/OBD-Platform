#pragma once
#include <math.h>
#include "vehicle_profile.h"

namespace obd {
// Estimated gear only: without a clutch sensor neutral/clutch cannot be proven.
// Zero means unknown, never neutral. Calibration is deliberately absent by default.
class GearEstimator {
public:
    static bool configured(const GearCalibration& c) {
        if (!c.calibrated || !c.count || c.count>6 || !isfinite(c.finalDrive) ||
            c.finalDrive<=0 || !isfinite(c.rollingCircumferenceM) || c.rollingCircumferenceM<=0) return false;
        for (uint8_t i=0;i<c.count;++i)
            if (!isfinite(c.ratios[i]) || c.ratios[i]<=0) return false;
        return true;
    }
    uint8_t update(const GearCalibration& c,float rpm,float speed,bool fresh,uint32_t now) {
        if (!configured(c) || !fresh || !isfinite(rpm) || !isfinite(speed) || rpm<600 || speed<8) return reset();
        const float observed=rpm/speed;
        uint8_t match=0;
        for (uint8_t i=0;i<c.count;++i) {
            const float expected=c.ratios[i]*c.finalDrive*1000.0f/(60.0f*c.rollingCircumferenceM);
            if (fabsf(observed/expected-1.0f)<=0.08f) {
                if (match) return reset(); // Ambiguous ratio bands.
                match=i+1;
            }
        }
        if (!match) return reset();
        if (candidate_!=match) { candidate_=match; since_=now; shown_=0; }
        if (uint32_t(now-since_)>=600) shown_=match;
        return shown_;
    }
    uint8_t reset() { candidate_=shown_=0;since_=0;return 0; }
private:
    uint8_t candidate_=0,shown_=0;
    uint32_t since_=0;
};
}
