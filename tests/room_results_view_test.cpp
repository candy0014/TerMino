#include "tetr/network/round_statistics.h"
#include "tetr/network/network_services.h"
#include "tetr/runtime/game_context.h"
#include "tetr/config/user_config.h"
#include "tetr/input/services.h"
#include "tetr/app/application_state.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/engine/multiplayer_scene.h"
#include "../src/app/multiplayer_battle.h"
#include "../src/app/settings_navigation.h"
#include "../src/app/ui_text.h"
#include "support/terminal_screen.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
namespace TetrTests::room_results_view_test {
int run(int argc,char** argv) {
    using namespace Network;
    RoundStatistics large;large.gameSeconds=123.125;large.realSeconds=4.5;large.pieceTime=true;
    large.score.score=std::numeric_limits<long long>::min();large.score.placedBlocks=20;large.score.totalAttack=12;
    large.score.totalReceivedAttack=std::numeric_limits<std::uint64_t>::max();large.score.roundPieceTypes={0,1};
    for(int lines=0;lines<2048;++lines)large.score.clearStatistics[{0,lines}]={std::numeric_limits<std::uint64_t>::max(),11,3};
    const auto wire=encode_round_statistics(large);const auto decoded=decode_round_statistics(wire);
    assert(decoded && decoded->gameSeconds==123.125 && decoded->pieceTime && decoded->score.score==large.score.score);
    assert(decoded->score.clearStatistics==large.score.clearStatistics && decoded->score.totalReceivedAttack==large.score.totalReceivedAttack);
    assert(!decode_round_statistics(wire.substr(0,wire.size()-1)) && !decode_round_statistics(wire+std::string(1,'\0')));
    auto parts=round_statistics_parts(wire);assert(parts.size()>1);
    ResultAssembler assembled;std::optional<std::string> complete;
    for(auto it=parts.rbegin();it!=parts.rend();++it)if(auto value=assembled.insert(*it))complete=std::move(value);
    assert(complete==wire);
    ResultAssembler broken;parts.back().back()^=1;for(const auto& part:parts)assert(!broken.insert(part));

    auto session=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;Runtime::GameContext c(*session,services);
    UserConfig::Settings user;Input::Services input;NetworkServices n;App::ApplicationState app;
    c.attach_network(n);c.attach_input(input);c.attach_user_settings(user);c.attach_app(app);app.primaryContext=&c;
    c.config.mode.mode=Runtime::GameMode::Multiplayer;c.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    n.multiplayerRoom=true;n.connection=ConnectionState::Connected;n.matchEpoch=7;n.matchPhase=RoomMatchPhase::Finished;n.roomPlayerIndex=0;n.winnerSlot=1;
    n.roomMembers={{0,"Alice",true,PlayerRole::Battle,true,true,false},{1,"Bob",false,PlayerRole::Battle,true,true,true}};
    n.playerSetups[1].width=10;n.playerSetups[1].visibleHeight=20;
    auto& p=session->player;p.gameplay.playfield.width=10;p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=20;p.gameplay.playfield.field.initialize(10,20);
    p.presentation.blockMaxWidth=p.presentation.blockMaxHeight=4;
    n.roundParticipants[7]={0,1};n.roundResultNames[7]={{0,"Alice"},{1,"Bob"}};
    RoundStatistics result;result.gameSeconds=2;result.realSeconds=2;result.score.placedBlocks=8;result.score.totalAttack=12;
    result.score.totalOutgoingAttack=7;result.score.totalReceivedAttack=3;result.score.roundPieceTypes={0};result.score.clearStatistics[{0,4}]={2,1,0};
    n.roundResultWires[7][0]=encode_round_statistics(result);result.score.placedBlocks=10;n.roundResultWires[7][1]=encode_round_statistics(result);
    Engine::initialize_multiplayer_scene(c);session->roomScene.finalists={0,1};session->roomScene.finalistLayout={0,1};
    user.keyCodes=UserConfig::default_key_codes();input.initialized=true;Input::refresh_bindings(input,user.keyCodes,user.additionalKeyCodes);
    App::UiText::set_language(App::UiText::Language::English);
    std::ostringstream output;auto* old=std::cout.rdbuf(output.rdbuf());Test::TerminalScreen screen;
    Input::KeyEventBuffer controlEvents;input.keyEventSource=&controlEvents;
    std::size_t consumed=0;int stage=0,quiet=0;bool detailed=false,clears=false,queuedEscape=false;
    using K=App::Settings::NavigationKey;const int retry=user.keyCodes[8];
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    input.keyStateProvider=[&](int key) {
        assert(std::chrono::steady_clock::now()<deadline);
        const auto text=output.str();if(text.size()>consumed){screen.feed(text.substr(consumed));consumed=text.size();}
        const auto content=screen.text();
        if(stage==0 && content.find("Room result summary")!=std::string::npos) {
            assert(content.find("PPS")!=std::string::npos && content.find("Received")!=std::string::npos);
            assert(content.find("360.000")!=std::string::npos && content.find("4.000")!=std::string::npos);
            detailed=content.find("TOTAL RECEIVED")!=std::string::npos;stage=1;quiet=0;
        } else if(stage==1 && content.find("> Bob")!=std::string::npos){stage=2;quiet=0;}
        else if(stage==2 && content.find("Room clear statistics")!=std::string::npos){clears=content.find("2;1;0")!=std::string::npos;stage=3;quiet=0;}
        else if(stage==3 && content.find("Room clear statistics")==std::string::npos && content.find("WIN")!=std::string::npos){stage=4;quiet=0;}
        if(++quiet<200)return false;
        const int wanted=stage==0 || stage==2?App::Settings::NavigationInput::key_code(K::Tab):
            stage==1?App::Settings::NavigationInput::key_code(K::Right):stage==3?App::Settings::NavigationInput::key_code(K::Back):retry;
        if(stage==3 && key==wanted && !queuedEscape) {
            controlEvents.update(wanted,true,std::chrono::steady_clock::now());
            controlEvents.update(wanted,false,std::chrono::steady_clock::now());queuedEscape=true;
        }
        return key==wanted;
    };
    App::install_start_screen_navigation(c);
    try {App::watch_room_match(c);}catch(...){std::cout.rdbuf(old);throw;}
    std::cout.rdbuf(old);assert(stage==4 && detailed && clears && queuedEscape && !app.startScreenRequested);
    // Closing the modal consumes both held Escape and its queued event; a
    // later, separate Escape still performs ordinary start-screen navigation.
    input.keyStateProvider=[](int){return false;};input.poll_control_interrupt();
    const int escape=App::Settings::NavigationInput::key_code(K::Back);
    controlEvents.update(escape,true,std::chrono::steady_clock::now());
    controlEvents.update(escape,false,std::chrono::steady_clock::now());
    bool returnedToStart=false;
    try {input.poll_control_interrupt();}catch(const App::StartScreenRequested&){returnedToStart=true;}
    assert(returnedToStart && app.startScreenRequested);
    std::cerr<<"room_results_view_test: multipart results, player selection, detailed summary, clear counts and Esc return OK\n";
    return 0;
}
} // namespace TetrTests::room_results_view_test
