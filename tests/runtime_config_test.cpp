#include "tetr/compat/legacy_encoding.h"
#include "tetr/runtime/runtime_config.h"

#include <cassert>
#include <iostream>

int main(){
    Runtime::RuntimeConfig a;
    assert(a.mode.mode==Runtime::GameMode::Zen&&!a.mode.replayMode&&a.mode.racingDistance==40);
    assert(a.handling.gravityInterval.interval().seconds()==0.6&&a.handling.das.seconds()==0.1&&a.handling.lockResetLimit==15);
    assert(a.handling.preMove && a.handling.dasPrecharge);
    assert(a.field.width==10&&a.field.height==20&&a.field.nextCount==5&&a.field.holdEnabled);
    assert(a.garbage.mode==Runtime::GarbageMode::None&&Compat::to_legacy(a.garbage.holePattern)==1&&a.garbage.incomingDelay.seconds()==0.3);
    assert(a.quickPlay.defaultFloor==1&&a.autoplay.mode==Runtime::AutoplayMode::Disabled&&a.autoplay.searchWidth==300);
    assert(!a.quickPlay.continuousCancellation);
    assert(a.display.squareStyle==0&&!a.display.obscuredColors);
    assert(a.rotationSystem==Runtime::RotationSystem::SrsPlus&&!a.randomPieces&&a.timeMultiplier==1.0);

    Runtime::RuntimeConfig b=a;
    b.field.width=37;
    b.quickPlay.allSpin=Runtime::AllSpinMode::Extended;
    b.handling.gravityInterval=Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(.125));
    assert(a.field.width==10&&a.quickPlay.allSpin==Runtime::AllSpinMode::Disabled&&a.handling.gravityInterval.interval().seconds()==.6);
    assert(b.field.width==37&&b.quickPlay.allSpin==Runtime::AllSpinMode::Extended&&b.handling.gravityInterval.interval().seconds()==.125);

    std::cout<<"runtime_config_test: OK\n";
}
