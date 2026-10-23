#ifndef TETR_RUNTIME_FIELD_VISIBILITY_H
#define TETR_RUNTIME_FIELD_VISIBILITY_H

#include "tetr/runtime/time.h"
#include <cmath>

namespace Runtime {

struct FieldVisibility {
    bool invisible=false,periodic=false;
    Duration delay{},interval=Duration::from_seconds(5),duration=Duration::from_seconds(1);
    friend bool operator==(const FieldVisibility& a,const FieldVisibility& b) noexcept {
        return a.invisible==b.invisible && a.periodic==b.periodic && a.delay==b.delay && a.interval==b.interval && a.duration==b.duration;
    }
    friend bool operator!=(const FieldVisibility& a,const FieldVisibility& b) noexcept {return !(a==b);}
};

inline bool periodic_invisible_flash(Duration elapsed,Duration interval,Duration duration) noexcept {
    if(duration<=Duration{} || elapsed<Duration{}) return false;
    if(interval<=Duration{}) return true;
    return elapsed>=interval && std::fmod(elapsed.seconds(),interval.seconds())<duration.seconds();
}

} // namespace Runtime
#endif
