#include "input.h"
#include "frame_worker.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/engine/player_input.h"
#include "tetr/input/services.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <iostream>
#include <future>

namespace TetrTests::frontend_handling_test {
namespace {
using Clock=std::chrono::steady_clock;
using namespace std::chrono;
Runtime::TimePoint tp(double t) {return Runtime::TimePoint::from_seconds(t);}
Runtime::Duration dur(double t) {return Runtime::Duration::from_seconds(t);}
const Clock::time_point origin{seconds(10)};
constexpr std::uint64_t sdlOrigin=7000000000;
Clock::time_point wall(double t) {return origin+duration_cast<Clock::duration>(duration<double>(t));}
Runtime::TimePoint game_time(Clock::time_point t) {return tp(duration<double>(t-origin).count());}
struct Fixture {
    bool sdl;
    Input::FocusState source;
    Input::Services services;
    Graphical::EventClock clock{sdlOrigin,origin};
    Engine::MovementInputState input;
    Engine::MovementState state;
    Engine::PlayerInputConfig config;
    Engine::MovementController movement{state,
        [](Piece::Pose p){return p.position.col>=0 && p.position.col<200 && p.position.row<=20;},
        [](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&){return false;}};
    explicit Fixture(bool useSdl,double das=.1,double arr=.01):sdl(useSdl) {
        UserConfig::Settings settings;settings.keyCodes=UserConfig::default_key_codes();
        settings.das=das;settings.arr=arr;
        const auto normalized=Runtime::normalize_user_config(settings,Runtime::selected_game_mode(settings));
        config.das=normalized.handling.das;config.arr=normalized.handling.arr;
        config.gravityInterval=Runtime::GravityInterval::disabled();
        state.pose={{0,100},Runtime::Rotation{}};
        services.bindings=Input::BindingSet::from_key_codes(settings.keyCodes);
        services.focusSource=&source;services.keyEventSource=&source.events();services.initialized=true;
        services.begin_gameplay_input();
        input.preparePiece(services.bindings,[](int){return false;},tp(0));
    }
    void key(SDL_Scancode scan,bool down,double t,bool repeat=false) {
        if(sdl) {
            SDL_KeyboardEvent event{};event.scancode=scan;event.down=down;event.repeat=repeat;
            event.timestamp=sdlOrigin+duration_cast<nanoseconds>(duration<double>(t)).count();
            Graphical::deliver_key(source,event,clock);
        } else {
            // Same monotonic timestamp and FocusState used by Linux evdev.
            source.update(Graphical::native_key(scan),down,wall(t));
        }
    }
    void frame(double t) {
        const std::vector<int> move{Graphical::native_key(SDL_SCANCODE_LEFT),Graphical::native_key(SDL_SCANCODE_RIGHT)};
        Engine::process_buffered_player_input_frame(input,movement,services,{},move,config,tp(t),game_time);
    }
    int shifts() const {return 100-state.pose.position.col;}
};
void thresholds() {
    for(bool sdl:{false,true})for(double das:{.05,.09,.1,.2})for(double arr:{0.,.004,.01,.02}) {
        Fixture f(sdl,das,arr);f.key(SDL_SCANCODE_LEFT,true,0);f.frame(0);
        assert(f.shifts()==1);
        f.key(SDL_SCANCODE_LEFT,true,das/2,true); // OS repeat cannot recharge or speed up DAS.
        f.frame(das-.000001);assert(f.shifts()==1);
        f.frame(das);assert(f.shifts()==(arr==0?100:2));
        if(arr>0) {f.frame(das+arr-.000001);assert(f.shifts()==2);f.frame(das+arr);assert(f.shifts()==3);}
        for(double held:{das-.000001,das+.000001}) {
            Fixture queued(sdl,das,arr);
            queued.key(SDL_SCANCODE_LEFT,true,0);queued.key(SDL_SCANCODE_LEFT,false,held);
            queued.frame(1);assert(queued.shifts()==(held<das?1:arr==0?100:2));
        }
    }
}
void sampled_short_taps() {
    // The engine runs between SDL polls. A release below DAS must still stop
    // at one cell when it is delivered by a later render/input iteration.
    int cases=0;
    for(int hz:{30,60,120,144,240,1000})for(double offset:{0.,.001,.007})
        for(double held:{.075,.080,.084,.086,.088,.089,.091,.095,.125,.275})
        for(double arr:{0.,.004,.01})for(auto key:{SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT}) {
        Fixture native(false,.09,arr),sdl(true,.09,arr);
        sdl.source.events().complete_sample(wall(0));
        const double press=.02+offset,release=press+held;
        bool nativeDown=false,nativeUp=false,sdlDown=false,sdlUp=false;
        double nextPoll=0;
        const int end=static_cast<int>((release+2./hz+.02)*2000)+1;
        for(int tick=0;tick<=end;++tick) {
            const double t=tick/2000.;
            if(!nativeDown && t>=press-1e-12) {native.key(key,true,press);nativeDown=true;}
            if(!nativeUp && t>=release-1e-12) {native.key(key,false,release);nativeUp=true;}
            if(t>=nextPoll-1e-12) {
                if(!sdlDown && t>=press-1e-12) {sdl.key(key,true,press);sdlDown=true;}
                if(!sdlUp && t>=release-1e-12) {sdl.key(key,false,release);sdlUp=true;}
                sdl.source.events().complete_sample(wall(t));
                nextPoll+=1./hz;
            }
            native.frame(t);sdl.frame(t);
            assert(std::abs(sdl.shifts())<=std::abs(native.shifts()));
        }
        assert(nativeUp && sdlUp && native.state.pose==sdl.state.pose);
        if(held<.09)assert(std::abs(sdl.shifts())==1);
        ++cases;
    }
    std::cout<<cases<<" sampled-input cases: short taps/held repeats match native at 30..1000 Hz\n";
}
void delayed_polling_and_game_thread() {
    for (double das : {.05,.09,.1,.2}) for (double arr : {0.,.004,.01,.02})
        for (auto key : {SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT}) {
        const auto sample=[&](Fixture& f,double t) {
            Graphical::complete_input_sample(f.source,f.clock,
                sdlOrigin+duration_cast<nanoseconds>(duration<double>(t)).count());
        };
        Fixture worker(true,das,arr);
        sample(worker,0);
        worker.key(key,true,.3);sample(worker,.31);
        worker.frame(.01); // Captured before the thread was descheduled.
        worker.frame(.31);
        assert(std::abs(worker.shifts())==1);
        worker.key(key,false,.3+das-.001);sample(worker,1);worker.frame(1);
        assert(std::abs(worker.shifts())==1);

        Fixture pump(true,das,arr);
        sample(pump,0);
        // A slow pump receives a key after its initial sampling boundary.
        // Keep the original timestamp and act in this batch, without an
        // extra render/input poll before the initial one-cell shift.
        Graphical::InputSample slowPump(sdlOrigin+200000000);
        SDL_KeyboardEvent event{};event.scancode=key;event.down=true;event.timestamp=sdlOrigin+300000000;
        slowPump.deliver(pump.source,event,pump.clock);slowPump.complete(pump.source,pump.clock);
        pump.frame(.31);assert(std::abs(pump.shifts())==1);
        sample(pump,.3+das-.001);pump.frame(1);
        assert(std::abs(pump.shifts())==1);
        pump.key(key,false,.3+das-.0005);sample(pump,1);pump.frame(1);
        assert(std::abs(pump.shifts())==1);

        Fixture handler(true,das,arr);
        sample(handler,0);handler.key(key,true,.3);sample(handler,.3);handler.frame(.3);
        // Pump at DAS-1 ms, then stall inside a UI/window event handler.
        // Its completion must publish the captured pump time, even though
        // the worker is now far beyond DAS and the release is not delivered.
        Graphical::InputSample slowHandler(sdlOrigin+
            duration_cast<nanoseconds>(duration<double>(.3+das-.001)).count());
        slowHandler.complete(handler.source,handler.clock);handler.frame(1);
        assert(std::abs(handler.shifts())==1);
        handler.input.preparePiece(handler.services.bindings,[](int){return false;},tp(.8));
        handler.frame(1); // Spawning also cannot advance the sampled hold.
        assert(std::abs(handler.shifts())==1);
        handler.key(key,false,.3+das-.0005);sample(handler,1);handler.frame(1);
        assert(std::abs(handler.shifts())==1);
    }
    std::cout<<"Slow SDL pumps/handlers and descheduled game threads: no premature DAS repeats\n";
}
void worker_failure_and_idle_shutdown() {
    std::atomic<bool> cleaned=false;
    Graphical::FrameWorker::Callbacks callbacks;
    callbacks.initialize=[] {throw std::runtime_error("initialization failed");};
    callbacks.shutdown=[&] {cleaned=true;};
    Graphical::FrameWorker failed(std::move(callbacks),{1200,800},30);
    failed.stop();assert(cleaned);
    bool caught=false;try{failed.feedback();}catch(const std::runtime_error&){caught=true;}
    assert(caught);
    std::promise<void> ready;auto next=ready.get_future();
    Graphical::FrameWorker::Callbacks idle;
    idle.draw=[&](const Graphical::FrameRequest&,bool){ready.set_value();return Graphical::Interaction{};};
    Graphical::FrameWorker sleeping(std::move(idle),{1200,800},1);
    assert(next.wait_for(seconds(2))==std::future_status::ready);
    const auto before=Clock::now();sleeping.stop();
    assert(Clock::now()-before<milliseconds(500)); // Stop interrupts even a one-second frame-rate wait.
}
void blocked_drawing() {
    assert(SDL_Init(SDL_INIT_EVENTS)); // No video subsystem or window.
    for(int rate:{30,60,240,1000})for(double held:{.08,.12}) {
        std::promise<void> entered,release;auto ready=entered.get_future();auto released=release.get_future().share();
        Graphical::FrameWorker::Callbacks callbacks;
        callbacks.draw=[&](const Graphical::FrameRequest&,bool){entered.set_value();released.wait();return Graphical::Interaction{};};
        Graphical::FrameWorker drawing(std::move(callbacks),{1200,800},rate,1);
        assert(ready.wait_for(seconds(2))==std::future_status::ready);
        Fixture f(true,.09,.004);
        Graphical::complete_input_sample(f.source,f.clock,sdlOrigin);
        const auto push=[&](bool down,double time,bool repeat=false) {
            SDL_Event event{};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
            event.key.scancode=SDL_SCANCODE_LEFT;event.key.down=down;event.key.repeat=repeat;
            event.key.timestamp=sdlOrigin+duration_cast<nanoseconds>(duration<double>(time)).count();
            assert(SDL_PushEvent(&event));
        };
        push(true,.01);push(true,.02,true);push(false,.01+held);
        Graphical::poll_input_events(f.source,f.clock);f.frame(1);
        assert(f.shifts()==(held<.09?1:9) && f.input.phase(Graphical::native_key(SDL_SCANCODE_LEFT))==Engine::KeyPhase::Released);
        drawing.submit({800,600});assert(!drawing.feedback()); // Input/mailbox do not wait for drawing.
        release.set_value();drawing.stop();assert(drawing.feedback()->frames==1);
    }
    worker_failure_and_idle_shutdown();SDL_Quit();
}

}
int run(int argc,char** argv) {
    delayed_polling_and_game_thread();thresholds();sampled_short_taps();blocked_drawing();
    std::cout<<"DAS 50/90/100/200 ms: no repeat 1 us early; ARR 0/4/10/20 ms; queued releases and autorepeat: OK\n";
    return 0;
}
} // namespace TetrTests::frontend_handling_test
