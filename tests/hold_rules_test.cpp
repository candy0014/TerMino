#include "tetr/autoplay/availability.h"
#include "tetr/bot/environment.h"
#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/game_loop_adapter.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/remote_board_state_adapter.h"
#include "tetr/engine/replay_application.h"
#include "tetr/engine/undo.h"
#include "tetr/input/services.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/playback_checkpoint.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "../src/engine/live_piece_controls.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace TetrTests::hold_rules_test {
namespace {
struct Fixture {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    Replay::ReplaySessionState replay;
    Input::Services input;
    Engine::PieceLoopState loop;
    bool holdDown=false;
    Fixture() {
        context.attach_replay(replay); context.attach_input(input);
        context.config=Runtime::normalize_user_config(UserConfig::Settings{},Runtime::GameMode::Zen);
        context.config.field.nextCount=0;
        Input::refresh_bindings(input,UserConfig::default_key_codes());
        input.keyStateProvider=[this](int key) {return holdDown && key==UserConfig::default_key_codes()[7];};
        auto& player=session.player;
        auto& board=player.gameplay.playfield;
        board.width=10; board.visibleHeight=board.activeHeight=20;
        board.nextCount=0; board.holdEnabled=true;
        player.presentation.margin=30;
        player.pieces.allowedPieces=Piece::types_for_rotation_system(context.config.rotationSystem);
        Engine::initialize_board(context,player);
        player.pieces.bag={{Piece::Type::from_index(0),false},{Piece::Type::from_index(1),false},
            {Piece::Type::from_index(2),false},{Piece::Type::from_index(3),false},
            {Piece::Type::from_index(4),false},{Piece::Type::from_index(5),false}};
    }
};
void config_round_trip() {
    const auto file=std::filesystem::temp_directory_path()/"tetr_hold_rules.ini";
    UserConfig::Settings user;
    user.persistencePath=file.string();
    for (bool infinite:{false,true}) for (int count:{0,1,2,1000000000}) {
        user.infiniteHold=infinite; user.holdCount=count;
        assert(UserConfig::save(user));
        UserConfig::Settings loaded;
        UserConfig::load(loaded,file.string());
        assert(loaded.infiniteHold==infinite && loaded.holdCount==count);
        for (auto mode:{Runtime::GameMode::Zen,Runtime::GameMode::QuickPlay}) {
            auto config=Runtime::normalize_user_config(loaded,mode);
            assert(config.field.infiniteHold==infinite && config.field.holdCount==count);
            const auto prefix=Replay::encode_config_prefix(config)+"}";
            assert((prefix.find("InfiniteHold")!=std::string::npos)==infinite);
            assert((prefix.find("HoldCount")!=std::string::npos)==(count!=1));
            Runtime::RuntimeConfig decoded;
            decoded.field.infiniteHold=true; decoded.field.holdCount=99;
            Replay::ConfigPrefixResult result;
            assert(Replay::decode_config_prefix(prefix,decoded,result));
            assert(decoded.field.infiniteHold==infinite && decoded.field.holdCount==count);
            assert(Bot::make_environment(config).holdEnabled==(infinite || count>0));
        }
    }
    user.infiniteHold=1; user.holdCount=0; user.qpNoHold=1;
    assert(!Runtime::hold_available(Runtime::normalize_user_config(user,Runtime::GameMode::QuickPlay).field));
    UserConfig::reset_section(user,UserConfig::Section::Game);
    assert(user.infiniteHold==0 && user.holdCount==1);
    user.qpNoHold=0;
    user.holdCount=-1;
    assert(Runtime::normalize_user_config(user,Runtime::GameMode::Zen).field.holdCount==0);
    user.holdCount=0;
    auto config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
    assert(!Autoplay::availability(config,Runtime::AutoplayMode::Mech));
    config.field.infiniteHold=true;
    assert(!Autoplay::availability(config,Runtime::AutoplayMode::Mech));
    std::filesystem::remove(file);
}
void play_hold_sequences() {
    for (bool playback:{false,true}) for (bool occupied:{false,true})
        for (bool infinite:{false,true}) for (int quota:{0,1,2,5}) {
            Fixture f;
            auto& player=f.session.player;
            f.context.config.field.holdCount=quota;
            f.context.config.field.infiniteHold=infinite;
            f.context.config.mode.replayMode=playback;
            if (occupied) player.gameplay.hold.piece=Piece::Type::from_index(6);
            Piece::Type expected=Engine::current_piece(player);
            Piece::Type expectedHold=player.gameplay.hold.piece;
            int uses=0, locks=0, totalUses=0;
            const int requested=infinite ? 25 : quota;
            const auto play=[&](Runtime::Playfield& field,Piece::Definition piece,Runtime::PlayerSessionState& p,bool continued) {
                assert(piece.type()==expected && p.gameplay.hold.piece==expectedHold);
                assert(p.gameplay.hold.used==uses && continued==(uses>0));
                assert(Runtime::hold_available(f.context.config.field,uses)==(infinite || uses<quota));
                assert(Render::hold_draw_mode(f.context,p)==((infinite || uses<quota)
                    ? Render::PieceDrawMode::Normal : Render::PieceDrawMode::Ghost));
                if (locks==2) return Rules::PlayResult::Retry;
                if (uses<requested) {
                    if (playback) {
                        Engine::apply_replay_gameplay_event(f.context,{Replay::OperationKind::Hold},field,p,piece);
                    } else {
                        Engine::MovementState state;
                        state.pose={{0,4},Runtime::Rotation{}};
                        Engine::MovementInputState input;
                        Engine::AutoplayCarryState carry;
                        const auto canPlace=[&](Piece::Pose pose) {return piece.can_place(pose,field);};
                        const auto rotate=[](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) {return false;};
                        Engine::MovementController movement(state,canPlace,rotate);
                        f.holdDown=true;
                        const auto result=Engine::process_live_piece_controls(f.context,field,piece,p,state,input,carry,
                            continued,state.pose,{}, {},movement,canPlace,rotate,[] {return Rules::PlayResult::ContinueFrame;});
                        assert(result.signal==Rules::PlayResult::Hold);
                        f.holdDown=false;
                    }
                    expected=expectedHold.valid() ? expectedHold : p.pieces.bag.at(1).type;
                    expectedHold=piece.type();
                    ++uses; ++totalUses;
                    return Rules::PlayResult::Hold;
                }
                if (!playback && !infinite) {
                    // A fresh Hold press after the quota is exhausted must be ignored.
                    Engine::MovementState state;
                    state.pose={{0,4},Runtime::Rotation{}};
                    Engine::MovementInputState input;
                    Engine::AutoplayCarryState carry;
                    const auto canPlace=[&](Piece::Pose pose) {return piece.can_place(pose,field);};
                    const auto rotate=[](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) {return false;};
                    Engine::MovementController movement(state,canPlace,rotate);
                    f.holdDown=true;
                    assert(Engine::process_live_piece_controls(f.context,field,piece,p,state,input,carry,continued,
                        state.pose,{}, {},movement,canPlace,rotate,[] {return Rules::PlayResult::ContinueFrame;}).signal
                        ==Rules::PlayResult::ContinueFrame);
                    f.holdDown=false;
                }
                ++locks; ++p.gameplay.scoring.placedBlocks;
                expected=p.pieces.bag.at(1).type;
                uses=0;
                return Rules::PlayResult::Locked;
            };
            assert(Engine::run_piece_loop(f.context,player,play,&f.loop)==Engine::GameLoopExit::Retry);
            assert(totalUses==2*requested && player.gameplay.hold.used==0);
            if (!playback) {
                assert(Engine::restore_previous_undo_snapshot(f.context));
                assert(player.gameplay.scoring.placedBlocks==1 && player.gameplay.hold.used==0);
            }
        }
}
void network_and_color() {
    Fixture f;
    auto& p=f.session.player;
    f.session.opponent.gameplay.playfield=p.gameplay.playfield;
    f.session.opponent.presentation.margin=90;
    auto& hold=p.gameplay.hold;
    hold.piece=Piece::Type::from_index(4);
    f.context.config.field.holdCount=2;
    for (int used:{0,1,2}) {
        hold.used=used;
        const auto wire=Network::encode_board_state(f.context,p);
        const auto snapshot=Network::decode_board_state(wire,10,20);
        assert(snapshot && snapshot->holdAvailable==std::optional<bool>{used<2});
        Engine::apply_remote_board_state(f.context,f.session.opponent,*snapshot);
        assert(Render::hold_draw_mode(f.context,f.session.opponent)==(used<2
            ? Render::PieceDrawMode::Normal : Render::PieceDrawMode::Ghost));
    }
    const auto locked=Network::encode_board_state(f.context,p,Network::ActivePieceState::locked(),
        Network::RemotePlacementSnapshot{Piece::Type::from_index(4)});
    const auto snapshot=Network::decode_board_state(locked,10,20);
    assert(snapshot && snapshot->placement && snapshot->holdAvailable==std::optional<bool>{true});
    Engine::apply_remote_board_state(f.context,f.session.opponent,*snapshot);
    assert(Render::hold_draw_mode(f.context,f.session.opponent)==Render::PieceDrawMode::Normal);
    auto bad=locked;
    const auto available=bad.find(";hold-v1;");assert(available!=std::string::npos);
    bad[available+9]='2'; // The clock extension now follows the Hold availability field.
    assert(!Network::decode_board_state(bad,10,20));
    hold.used=0; f.context.config.field.holdCount=1;
    const auto legacy=Network::decode_board_state(Network::encode_board_state(f.context,p),10,20);
    assert(legacy && !legacy->holdAvailable);
    f.context.config.field.holdCount=0;
    Engine::apply_remote_board_state(f.context,f.session.opponent,*legacy);
    assert(Render::hold_draw_mode(f.context,f.session.opponent)==Render::PieceDrawMode::Normal);
    f.context.config.field.holdCount=1;
    hold.used=500; f.context.config.field.infiniteHold=true;
    assert(Render::hold_draw_mode(f.context,p)==Render::PieceDrawMode::Normal);
    f.context.config.field.holdEnabled=false;
    assert(Render::hold_draw_mode(f.context,p)==Render::PieceDrawMode::Ghost);
}
void checkpoint_restores_hold() {
    Fixture f;
    f.context.config.mode.replayMode=true;
    f.context.config.field.holdCount=3;
    f.replay.playback.started=true;
    f.session.player.gameplay.hold={Piece::Type::from_index(4),2,{}};
    Replay::configure_playback_checkpoint_cache(f.context,"multi-hold");
    Replay::capture_playback_checkpoint(f.context);
    f.session.player.gameplay.hold={};
    Replay::schedule_playback_checkpoint_restore(f.context,{});
    assert(Replay::restore_scheduled_playback_checkpoint(f.context));
    assert(f.session.player.gameplay.hold.used==2 && f.session.player.gameplay.hold.piece==Piece::Type::from_index(4));
    assert(Render::hold_draw_mode(f.context,f.session.player)==Render::PieceDrawMode::Normal);
}
}
int run(int argc,char** argv) {
    std::ostringstream output;
    auto* previous=std::cout.rdbuf(output.rdbuf());
    config_round_trip(); play_hold_sequences(); network_and_color(); checkpoint_restores_hold();
    std::cout.rdbuf(previous);
    std::cout<<"hold_rules_test: OK\n";
    return 0;
}
} // namespace TetrTests::hold_rules_test
