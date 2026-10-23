#include "tetr/engine/garbage_service.h"
#include "tetr/engine/frame_board_adapter.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/engine/replay_application.h"
#include "tetr/engine/undo.h"
#include "tetr/rules/frame_update.h"
#include "tetr/rules/quick_play.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "support/terminal_screen.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace TetrTests::qp_pressure_test {
namespace {
struct Capture {
    std::ostringstream stream;
    std::streambuf* old=std::cout.rdbuf(stream.rdbuf());
    ~Capture() { std::cout.rdbuf(old); }
};
struct Rig {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    Replay::ReplaySessionState replay;
    Runtime::PlayerSessionState& player=session.player;
    Runtime::Playfield& field=player.gameplay.playfield.field;
    Rig() {
        context.attach_replay(replay);
        context.config.simulationPps=1;
        context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
        context.config.garbage.incomingDelay=Runtime::Duration::from_seconds(5);
        session.gameStartTime=Runtime::TimePoint{};
        auto& b=player.gameplay.playfield;
        b.width=10; b.activeHeight=b.visibleHeight=20; b.holdEnabled=true;
        field.initialize(10,20);
        player.presentation.margin=50;
        player.presentation.blockMaxHeight=2; player.presentation.blockMaxWidth=3;
        player.garbage.random.seed(123); player.attackRoundingRandom.seed(42);
        Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
    }
    void at(double seconds) { session.simulationTime=Runtime::TimePoint::from_seconds(seconds); }
    void receive(int attack) { Engine::GarbageService::add_qp_opponent_attack(context,player,field,attack); }
    void due() {
        auto s=Engine::capture_frame_rule_state(player,context);
        const auto c=Engine::make_frame_rule_config(context,Runtime::elapsed_game_time(session,context.now()));
        const auto result=Rules::advance_frame_before_presentation(s,c,{},services.random);
        Engine::store_frame_rule_state(player,s,context);
        assert(result.incomingGarbagePackets.empty());
        for (int attack:result.resolvedQpGarbagePackets)
            Engine::GarbageService::add_buffer(context,player,field,attack,false,attack);
    }
    int pending() const { return Rules::pending_garbage(player.garbage.buffer); }
};
void timing_thresholds_and_reception() {
    Capture capture;
    Rig r;
    r.receive(8); assert(r.pending()==0);
    assert(r.session.quickPlay.progress.windupWarnings.front().segments==2);
    r.at(.2); r.receive(8);
    assert(r.session.quickPlay.progress.windupWarnings[1].start==3);
    r.at(.999); r.due(); assert(r.pending()==0);
    r.at(1); r.due(); assert(r.pending()==4);
    r.at(1.5); r.due(); assert(r.pending()==8);
    r.at(3.999); r.due(); assert(r.pending()==8);
    r.at(4); r.due(); assert(r.pending()==12);
    r.at(4.5); r.due(); assert(r.pending()==16);
    assert(r.player.garbage.buffer.front().arrival_time().seconds()==1);
    assert(!Engine::GarbageService::has_ready_buffer(r.context,r.player));
    Rig vl;
    vl.context.config.quickPlay.volatileMode=Runtime::ModifierMode::Enabled;
    vl.receive(7); assert(vl.pending()==14 && vl.session.quickPlay.progress.windups.empty());
    vl.session.quickPlay.progress.receiveProtection=0;
    vl.receive(8); assert(vl.pending()==14 && vl.session.quickPlay.progress.windups.size()==2);
    vl.at(1); vl.due(); assert(vl.pending()==22); // 8, not 16: reception multiplier runs once.
    Rig rv;
    rv.context.config.quickPlay.volatileMode=Runtime::ModifierMode::Reverse;
    rv.receive(4); assert(rv.pending()==12 && rv.session.quickPlay.progress.windups.empty());
    Rig protectedRun;
    protectedRun.session.quickPlay.progress.receiveProtection=18;
    protectedRun.receive(12); assert(protectedRun.pending()==6); // Protect BEFORE checking Windup.
    Rig cap;
    cap.receive(1000); cap.at(10); cap.due(); assert(cap.pending()==16);
    Rig cancelled;
    cancelled.context.config.quickPlay.continuousCancellation=true;
    cancelled.session.quickPlay.progress.cancellation=1000;
    cancelled.receive(5); assert(cancelled.pending()==5); // Floor-1 cap does not amplify a larger base.
}
void replay_and_undo() {
    Capture capture;
    Rig live,playback;
    live.receive(8);
    Engine::begin_undo_history(live.context);
    live.at(1.5); live.due(); assert(live.pending()==8);
    assert(Engine::restore_previous_undo_snapshot(live.context));
    assert(live.pending()==0 && live.session.quickPlay.progress.windups.size()==2);
    live.at(1.5); live.due(); assert(live.pending()==8);
    playback.context.config=live.context.config; playback.context.config.mode.replayMode=true;
    playback.replay.separateGameTime=true; playback.replay.gameClockPaused=true;
    auto piece=Piece::get_definition(Piece::Type::from_index(0));
    auto ops=Replay::decode_operation_stream(live.replay.data,0);
    while (!ops.empty()) {
        const auto op=ops.front(); ops.pop();
        playback.replay.lastGameTimestamp=op.gameTimestamp.value_or(op.timestamp);
        Engine::apply_replay_gameplay_event(playback.context,op,playback.field,playback.player,piece);
    }
    assert(playback.pending()==live.pending());
    assert(playback.player.attackRoundingRandom==live.player.attackRoundingRandom);
    assert(playback.session.quickPlay.progress.windupWarnings.size()==1);
    assert(playback.session.quickPlay.progress.windups.empty()); // Explicit replay receptions only.
}
void pressure_statistics_and_frame_partition() {
    Rules::FrameRuleConfig c;
    c.garbageMode=Runtime::GarbageMode::QuickPlay;
    c.elapsedGameTime=Runtime::Duration::from_seconds(.5);
    c.ownAttackApm=80;
    for (unsigned seed=0;seed<100;++seed) {
        Rules::FrameRuleState whole,split;
        whole.qpAltitude=split.qpAltitude=1000;
        whole.qpFloor=split.qpFloor=7;
        std::mt19937 a(seed),b(seed);
        const auto expected=Rules::advance_frame_before_presentation(whole,c,Runtime::Duration::from_seconds(.5),a);
        std::vector<int> actual;
        for (int frame=1;frame<=30;++frame) {
            split.qpAltitude=1000;
            c.elapsedGameTime=Runtime::Duration::from_seconds(frame/60.0);
            const auto result=Rules::advance_frame_before_presentation(split,c,Runtime::Duration::from_seconds(1.0/60),b);
            actual.insert(actual.end(),result.incomingGarbagePackets.begin(),result.incomingGarbagePackets.end());
        }
        c.elapsedGameTime=Runtime::Duration::from_seconds(.5);
        assert(expected.incomingGarbagePackets==actual && a==b);
    }
    // Independent runs, not one run's fixed latent intensity, must recover
    // both fitted moments. Grouping line credits must not multiply variance
    // by the mean packet size. This catches the former Poisson-packet model.
    for(const auto sample:std::vector<std::pair<double,double>>{{100,20},{500,40},{1000,80}}) {
        const auto [altitude,ownApm]=sample;
        const auto model=Rules::qp_pressure(altitude,ownApm);
        constexpr int runs=512,frames=14400; // Four minutes per independent run.
        double sum=0,squares=0;
        c.ownAttackApm=ownApm;c.elapsedGameTime=Runtime::Duration::from_seconds(10);
        for(int run=0;run<runs;++run) {
            std::mt19937 random(12000+run);
            Rules::FrameRuleState s;s.qpFloor=Rules::qp_floor(altitude);
            int total=0;
            for(int frame=0;frame<frames;++frame) {
                s.qpAltitude=altitude;
                const auto result=Rules::advance_frame_before_presentation(s,c,Runtime::Duration::from_seconds(1./60),random);
                for(int attack:result.incomingGarbagePackets)total+=attack;
            }
            sum+=total;squares+=double(total)*total;
        }
        const double mean=sum/runs,variance=(squares-sum*sum/runs)/(runs-1);
        const double expected=model.mean*4,expectedVariance=0.9375553655037346*expected+
            model.relativeVariance*expected*expected;
        std::cerr<<"QP2 "<<altitude<<"m / own "<<ownApm<<" APM: mean "<<mean<<" / "<<expected
            <<", variance "<<variance<<" / "<<expectedVariance<<'\n';
        assert(std::abs(mean-expected)<expected*.10);
        assert(std::abs(variance-expectedVariance)<expectedVariance*.28);
    }
    // Invalid external predictors and unsupported tails stay finite.
    for(double h:{-1.,0.,6000.,1e20,std::nan("")})for(double a:{-1.,0.,500.,1e20,std::nan("")}) {
        const auto p=Rules::qp_pressure(h,a);
        assert(std::isfinite(p.mean) && p.mean>=1 && p.mean<=180.00001);
        assert(std::isfinite(p.relativeVariance) && p.relativeVariance>0 && p.relativeVariance<=1);
    }
}
void warning_is_visible_and_cached() {
    Rig r;
    r.receive(12);
    Test::TerminalScreen screen;
    { Capture c; Render::redraw_player_board(r.context,r.player); screen.feed(c.stream.str()); }
    const auto warning=r.player.presentation.windupDisplay;
    const auto holdColumn=[&] {
        return r.player.presentation.margin-(r.player.presentation.blockMaxWidth+1)*2-4;
    };
    assert(warning[0].width==12 && warning[0].column+12+2==holdColumn());
    assert(screen.position_of("WINDUP").first!=0);
    const auto firstColor=screen.at(warning[0].row+1,warning[0].column+4).foreground;
    const auto readBars=[&] {
        std::string text;
        for (int x=0;x<12;++x) text+=screen.at(warning[1].row+1,warning[1].column+x+1).glyph;
        return text;
    };
    const auto bars=readBars();
    assert(std::count(bars.begin(),bars.end(),'!')==3);
    { Capture c; Render::print_windup(r.context,r.player); assert(c.stream.str().empty()); }
    r.at(.3);
    { Capture c; Render::print_windup(r.context,r.player); screen.feed(c.stream.str()); }
    assert(screen.at(warning[0].row+1,warning[0].column+4).foreground!=firstColor);
    assert(readBars().find("! ! !")!=std::string::npos);
    { Capture c;
      Render::print_spin(r.context,r.player,Piece::Type::from_index(0),false);
      Render::print_clear(r.context,r.player,4);
      Render::print_combo(r.context,r.player);
      Render::print_spike(r.context,r.player);
      Render::print_message(r.context,r.player);
      screen.feed(c.stream.str()); }
    assert(screen.position_of("WINDUP").first!=0);
    r.at(3.5); { Capture c; r.due(); }
    { Capture c; Render::redraw_player_board(r.context,r.player); screen.feed(c.stream.str()); }
    assert(screen.position_of("WINDUP").first==0); // Full repaint also erases an expired/undone box.
    r.receive(12);
    { Capture c; Render::print_windup(r.context,r.player); screen.feed(c.stream.str()); }
    { Capture c; Render::erase_player_board(r.context,r.player); screen.feed(c.stream.str()); }
    assert(screen.position_of("WINDUP").first==0);
    for (int size:{4,10,20}) {
        r.player.gameplay.playfield.width=size;
        r.player.gameplay.playfield.visibleHeight=r.player.gameplay.playfield.activeHeight=size;
        r.field.initialize(size,size);
        Engine::center_game_boards(r.context,{100,60});
        const auto extent=Render::board_horizontal_extent(r.context,r.player,false);
        { Capture c; Render::redraw_player_board(r.context,r.player); }
        assert(extent.left>=0 && r.player.presentation.windupDisplay[0].column>=extent.left);
        assert(r.player.presentation.windupDisplay[0].column+12+2==holdColumn());
    }
    const auto requirement=Engine::game_terminal_requirements(r.context);
    for (int columns=requirement.minimum.columns;columns<=requirement.minimum.columns+16;++columns) {
        Engine::center_game_boards(r.context,{columns,80});
        const auto core=Render::board_core_horizontal_extent(r.context,r.player,false);
        assert(core.left>=1 && core.right<=columns-1);
        { Capture c; Render::redraw_player_board(r.context,r.player); }
        for (const auto& span:r.player.presentation.windupDisplay)
            assert(span.width==12 && span.column==std::max(0,holdColumn()-14));
    }
    r.player.gameplay.hold.piece=Piece::Type::from_index(0);
    for (int columns=requirement.minimum.columns-12;columns<requirement.minimum.columns;++columns) {
        Engine::center_game_boards(r.context,{columns,80});
        r.at(3.5);
        Test::TerminalScreen holdOnly;
        { Capture c; Render::print_border(r.context,r.player);
          Render::draw_board_hold(r.context,r.player); holdOnly.feed(c.stream.str()); }
        Test::TerminalScreen compact;
        { Capture c; Render::redraw_player_board(r.context,r.player); compact.feed(c.stream.str()); }
        assert(holdColumn()-14<0);
        assert(r.player.presentation.windupDisplay[0].column==0);
        assert(r.player.presentation.windupDisplay[0].width==static_cast<std::size_t>(std::clamp(holdColumn(),0,12)));
        const auto checkHold=[&] {
            const auto& p=r.player.presentation;
            for (int y=p.topMargin;y<=p.topMargin+p.hold_panel_height();++y)
                for (int x=std::max(0,holdColumn());x<p.margin-2;++x) {
                    const auto expected=holdOnly.at(y+1,x+1),actual=compact.at(y+1,x+1);
                    assert(actual.glyph==expected.glyph && actual.background==expected.background);
                    if (expected.glyph!=" ") assert(actual.foreground==expected.foreground);
                }
        };
        checkHold();
        r.at(3.8);
        { Capture c; Render::print_windup(r.context,r.player); compact.feed(c.stream.str()); }
        checkHold(); // A color pulse cannot paint over the held piece or border.
        r.at(10);
        { Capture c; Render::print_windup(r.context,r.player); compact.feed(c.stream.str()); }
        checkHold(); // Expiration cannot erase HOLD either.
        for (const auto& span:r.player.presentation.windupDisplay) assert(span.width==0);
    }
}
void fixtures() {
    std::ostringstream out; out<<"[";
    bool first=true;
    for (int amount:{7,8,11,21,100}) for (int altitude:{0,3999,4000,4500}) for (bool vl:{false,true}) {
        Runtime::QuickPlayProgress p;
        Rules::qp_queue_windup(p,amount,altitude,vl,0);
        Rules::qp_queue_windup(p,amount,altitude,vl,.2);
        if (!first) out<<','; first=false;
        out<<"{\"amount\":"<<amount<<",\"altitude\":"<<altitude<<",\"vl\":"<<vl<<",\"until\":"<<p.windupUntil<<",\"segments\":[";
        for (std::size_t i=0;i<p.windups.size();++i) {
            if (i) out<<','; out<<'['<<p.windups[i].attack<<','<<p.windups[i].due<<']';
        }
        out<<"]}";
    }
    std::cout<<out.str()<<"]\n";
}
}
int run(int argc,char** argv) {
    if (argc>1 && std::string(argv[1])=="--fixtures") { fixtures(); return 0; }
    timing_thresholds_and_reception(); replay_and_undo();
    pressure_statistics_and_frame_partition(); warning_is_visible_and_cached();
    return 0;
}
} // namespace TetrTests::qp_pressure_test
