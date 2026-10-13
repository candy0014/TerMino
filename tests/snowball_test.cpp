#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/field_mutation.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/input/services.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/playback_checkpoint.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/session_state.h"
#include "tetr/rules/attack.h"
#include "tetr/rules/garbage_generation.h"
#include "tetr/rules/quick_play.h"
#include "tetr/runtime/game_context.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string_view>
#include <utility>

namespace {
using Runtime::ModifierMode;

// Explicit examples of floor(30% * width)..ceil(40% * width), widths 4..20.
constexpr std::array<std::pair<int,int>,17> kReverseGrayBounds={{
    {1,2},{1,2},{1,3},{2,3},{2,4},{2,4},{3,4},{3,5},{3,5},
    {3,6},{4,6},{4,6},{4,7},{5,7},{5,8},{5,8},{6,8}
}};

struct Capture {
    std::ostringstream stream;
    std::streambuf* previous=std::cout.rdbuf(stream.rdbuf());
    ~Capture() { std::cout.rdbuf(previous); }
};

// Apply the renderer's ANSI cursor output to a screen so we can detect text
// erased from neighboring panels, including UTF-8 piece cells.
struct TerminalGrid {
    std::array<std::array<char32_t,160>,160> cells{};
    std::array<std::array<unsigned,160>,160> colors{};
    unsigned color=0;
    int row=0, column=0;
    TerminalGrid() { for (auto& line:cells) line.fill(U' '); }
    std::pair<int,int> find_text(std::u32string_view text) const {
        for (int row=0; row<160; ++row)
            for (int col=0; col+static_cast<int>(text.size())<=160; ++col)
                if (std::equal(text.begin(),text.end(),cells[row].begin()+col)) return {row,col};
        return {-1,-1};
    }
    template<class Draw> void draw(Draw&& action) {
        Capture capture;
        action();
        const auto output=capture.stream.str();
        for (std::size_t i=0; i<output.size();) {
            const auto byte=static_cast<unsigned char>(output[i++]);
            if (byte==0x1b && i<output.size() && output[i]=='[') {
                ++i;
                std::vector<int> arguments(1,0);
                while (i<output.size()) {
                    const char ch=output[i++];
                    if (ch>='0' && ch<='9') arguments.back()=arguments.back()*10+ch-'0';
                    else if (ch==';') arguments.push_back(0);
                    else {
                        if (ch=='H') {
                            row=std::max(1,arguments[0])-1;
                            column=arguments.size()>1 ? std::max(1,arguments[1])-1 : 0;
                        } else if (ch=='m' && arguments.size()==5 && arguments[0]==38 && arguments[1]==2) {
                            color=(arguments[2]<<16)|(arguments[3]<<8)|arguments[4];
                        }
                        break;
                    }
                }
                continue;
            }
            if (byte=='\n') { ++row; column=0; continue; }
            if (byte=='\r') { column=0; continue; }
            char32_t cell=byte;
            if (byte>=0xc0) {
                const int continuation=byte<0xe0 ? 1 : byte<0xf0 ? 2 : 3;
                cell=byte&((1<<(6-continuation))-1);
                for (int j=0; j<continuation; ++j) cell=(cell<<6)|(output[i++]&0x3f);
            }
            assert(row>=0 && row<160 && column>=0 && column<160);
            colors[row][column]=color;
            cells[row][column++]=cell;
        }
    }
};

struct Fixture {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    Replay::ReplaySessionState replay;
    Input::Services input;
    Fixture(UserConfig::Settings user={}) {
        user.qpSnowball=user.qpSnowball ? user.qpSnowball : 1;
        context.config=Runtime::normalize_user_config(user,Runtime::GameMode::QuickPlay);
        context.attach_replay(replay);
        context.attach_input(input);
        input.keyStateProvider=[](int) { return false; };
        auto& player=session.player;
        auto& board=player.gameplay.playfield;
        board.width=context.config.field.width;
        board.visibleHeight=context.config.field.height;
        board.nextCount=context.config.field.nextCount;
        board.holdEnabled=context.config.field.holdEnabled;
        player.presentation.margin=30;
        player.pieces.allowedPieces=Piece::types_for_rotation_system(context.config.rotationSystem);
        player.pieces.random.seed(123);
        player.garbage.random.seed(456);
        Engine::initialize_board(context,player);
        Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
        Engine::initialize_quick_play(context,player);
        Engine::initialize_board_timers(context,player);
    }
};

void combinations_start_empty() {
    int UserConfig::Settings::* mods[]={&UserConfig::Settings::qpExpert,&UserConfig::Settings::qpNoHold,
        &UserConfig::Settings::qpMessy,&UserConfig::Settings::qpGravity,&UserConfig::Settings::qpVolatile,
        &UserConfig::Settings::qpDoubleHole,&UserConfig::Settings::qpInvisible,&UserConfig::Settings::qpAllSpin,
        &UserConfig::Settings::qpBackfire,&UserConfig::Settings::qpSnakeEyes};
    for (int sb:{1,2}) for (int fool=0; fool<=4; ++fool) for (int variant=0; variant<21; ++variant) {
        UserConfig::Settings user;
        user.qpSnowball=sb;
        user.qpFool=fool;
        user.fieldWidth=99; user.fieldHeight=99;
        user.das=0.25; user.arr=0.08;
        if (variant<20) user.*mods[variant/2]=1+variant%2;
        else for (auto mod:mods) user.*mod=2;
        Fixture fixture(user);
        const auto& cfg=fixture.context.config;
        const auto& player=fixture.session.player;
        const auto& board=player.gameplay.playfield;
        assert(cfg.field.width==4 && cfg.field.height==4);
        assert(board.width==4 && board.visibleHeight==4 && board.activeHeight==4);
        assert(board.field.width()==4 && board.field.height()==4);
        assert(board.garbageHeight==0 && board.solidGarbageHeight==0);
        assert(player.garbage.buffer.empty());
        for (int row=-Runtime::kHiddenRowCapacity; row<4; ++row)
            for (int col=0; col<4; ++col) assert(board.field.cell(row,col).empty_cell());
        assert(std::fabs(cfg.handling.das.seconds()-(sb==2 ? 0.0 : 0.25))<1e-12);
        assert(std::fabs(cfg.handling.arr.seconds()-(sb==2 ? 0.0 : 0.08))<1e-12);
        if (user.qpNoHold) assert(!cfg.field.holdEnabled);
        if (user.qpNoHold==2) assert(cfg.field.nextCount==1 && !cfg.field.ghost && cfg.randomPieces);
        if (user.qpGravity==2) assert(cfg.handling.gravityInterval.is_instant());
        if (user.qpMessy==2) assert(cfg.field.lineClearDelay.seconds()==70.0/60.0);
        if (user.qpInvisible) assert(cfg.field.invisible);
        if (user.qpVolatile) assert(cfg.garbage.attackMultiplier==(user.qpVolatile==2 ? 3 : 2));
    }
    UserConfig::Settings user;
    user.qpVolatile=2;
    const auto ordinary=Runtime::normalize_user_config(user,Runtime::GameMode::QuickPlay);
    assert(ordinary.field.width==10 && ordinary.field.height==14);
}

void growth_preserves_state_and_stops() {
    Fixture fixture;
    auto& player=fixture.session.player;
    auto& board=player.gameplay.playfield;
    auto& progress=fixture.session.quickPlay.progress;
    board.field.set_cell(3,1,Runtime::BoardCell::piece(2));
    board.field.set_cell(-1,2,Runtime::BoardCell::garbage());
    Engine::GarbageService::add_buffer(fixture.context,player,board.field,2,false,2);
    const auto random=player.garbage.random;
    const auto queue=player.pieces.bag;
    const auto hold=player.gameplay.hold.piece;
    progress.quadColumns={1,-1}; progress.szColumns={0,3};
    player.gameplay.scoring.totalLines=19;
    assert(!Engine::grow_snowball_board(fixture.context,player));
    player.gameplay.scoring.totalLines=20;
    assert(Engine::grow_snowball_board(fixture.context,player));
    assert(board.width==5 && board.visibleHeight==5 && board.activeHeight==5);
    assert(board.field.cell(4,1)==Runtime::BoardCell::piece(2));
    assert(board.field.cell(0,2).is_normal_garbage());
    assert(board.field.cell(4,4).empty_cell());
    assert(!Engine::grow_snowball_board(fixture.context,player));
    player.gameplay.scoring.totalLines=40;
    assert(Engine::grow_snowball_board(fixture.context,player));
    assert(board.field.cell(5,2)==Runtime::BoardCell::piece(2));
    assert(board.field.cell(1,3).is_normal_garbage());
    assert(board.field.cell(5,0).empty_cell());
    assert((progress.quadColumns==std::array<int,2>{2,-1}));
    assert((progress.szColumns==std::array<int,2>{1,4}));
    assert(player.pieces.bag==queue && player.gameplay.hold.piece==hold);
    assert(player.garbage.random==random);
    assert(player.garbage.buffer.size()==1 && player.garbage.buffer.front().line_count()==2);
    auto holes=player.garbage.holeOrder;
    std::sort(holes.begin(),holes.end());
    assert((holes==std::vector<int>{0,1,2,3,4,5}));
    player.gameplay.scoring.totalLines=319;
    assert(Engine::grow_snowball_board(fixture.context,player));
    assert(board.width==19);
    player.gameplay.scoring.totalLines=320;
    assert(Engine::grow_snowball_board(fixture.context,player));
    assert(board.width==20 && board.visibleHeight==20);
    player.gameplay.scoring.totalLines=1000;
    assert(!Engine::grow_snowball_board(fixture.context,player));
    assert(fixture.context.config.field.width==20 && fixture.context.config.field.height==20);
    Engine::GarbageService::add_garbage(fixture.context,player,board.field,1);
    assert(player.garbage.holeOrder.size()==20);
    // Reinitialization returns to the original size, including after a seek.
    Engine::initialize_board(fixture.context,player);
    assert(board.width==4 && board.visibleHeight==4 && board.activeHeight==4);
    assert(fixture.context.config.field.width==4 && fixture.context.config.field.height==4);
}

void solid_and_countdown_rows_remain_barriers() {
    Fixture fixture;
    auto& player=fixture.session.player;
    auto& board=player.gameplay.playfield;
    Engine::GarbageService::add_solid_garbage(fixture.context,player,board.field,1,Runtime::BoardCell::timed_solid());
    Engine::GarbageService::add_solid_garbage(fixture.context,player,board.field,1);
    assert(board.activeHeight==2);
    player.gameplay.scoring.totalLines=20;
    assert(Engine::grow_snowball_board(fixture.context,player));
    assert(board.activeHeight==3 && board.solidGarbageHeight==1);
    assert(board.field.cell(3,4).kind()==Runtime::BoardCellKind::TimedSolid);
    for (int col=0; col<5; ++col) assert(board.field.cell(4,col).kind()==Runtime::BoardCellKind::SolidGarbage);
    Engine::TimedSolidDecayResult result;
    for (int i=0; i<6; ++i) {
        result=Engine::decay_timed_solid_garbage(5,-Runtime::kHiddenRowCapacity,5,
            [&](int row,int col) { return board.field.cell(row,col); },
            [&](int row,int col,Runtime::BoardCell cell) { board.field.set_cell(row,col,cell); });
    }
    assert(result.releasedRows==1);
    assert(board.field.cell(3,4).is_normal_garbage());
}

void garbage_rows_follow_mods_when_growing() {
    bool sawAddedGray=false,sawAddedHole=false;
    for (int sb:{1,2}) for (int dh:{0,1,2}) for (int nh:{0,1,2}) for (unsigned seed=0; seed<16; ++seed) {
        UserConfig::Settings user;
        user.qpSnowball=sb; user.qpDoubleHole=dh; user.qpNoHold=nh;
        Fixture fixture(user);
        auto& player=fixture.session.player;
        auto& board=player.gameplay.playfield;
        player.garbage.random.seed(seed);
        // Include hidden garbage and a partially filled hole, while a separate
        // player-built row must continue to gain empty space.
        Engine::GarbageService::add_garbage(fixture.context,player,board.field,6);
        if (dh==2) for (int row=-2; row<4; ++row) {
            int gray=0;
            for (int col=0; col<4; ++col) gray+=board.field.cell(row,col).is_normal_garbage();
            assert(gray>=1 && gray<=2);
        }
        for (int col=0; col<4; ++col) if (board.field.cell(-2,col).empty_cell()) {
            board.field.set_cell(-2,col,Runtime::BoardCell::piece(2));
            break;
        }
        board.field.set_cell(-3,1,Runtime::BoardCell::piece(1));
        for (int size=5; size<=20; ++size) {
            const int offset=(size-4)/2-(size-5)/2;
            const auto oldField=board.field;
            const auto random=player.garbage.random;
            player.gameplay.scoring.totalLines=(size-4)*20;
            assert(Engine::grow_snowball_board(fixture.context,player));
            for (int row=size-6; row<size; ++row) {
                int oldEmpty=0,newEmpty=0,gray=0;
                for (int col=0; col<size-1; ++col) {
                    assert(board.field.cell(row,col+offset)==oldField.cell(row-1,col));
                    oldEmpty+=oldField.cell(row-1,col).empty_cell();
                }
                for (int col=0; col<size; ++col) {
                    newEmpty+=board.field.cell(row,col).empty_cell();
                    gray+=board.field.cell(row,col).is_normal_garbage();
                }
                if (dh==2) {
                    const auto [minimum,maximum]=kReverseGrayBounds[size-4];
                    assert(gray>=minimum && gray<=maximum);
                    const auto added=board.field.cell(row,offset ? 0 : size-1);
                    sawAddedGray|=added.is_normal_garbage(); sawAddedHole|=added.empty_cell();
                } else assert(newEmpty==oldEmpty);
            }
            const int playerRow=size-7;
            const int newColumn=offset ? 0 : size-1;
            assert(board.field.cell(playerRow,newColumn).empty_cell());
            if (dh!=2) assert(player.garbage.random==random);
        }
    }
    assert(sawAddedGray && sawAddedHole);
}

void reverse_garbage_density_at_all_widths() {
    for (int size=4; size<=20; ++size) {
        const auto [minimum,maximum]=kReverseGrayBounds[size-4];
        Rules::GarbageGenerationConfig config;
        config.width=size;
        config.garbageMode=Runtime::GarbageMode::QuickPlay;
        config.doubleHoleMode=ModifierMode::Reverse;
        Rules::GarbageGenerationState state;
        std::mt19937 random(123);
        std::array<bool,21> sawNormal{},sawTimed{};
        for (int i=0; i<1024; ++i) {
            const auto normal=Rules::generate_garbage_row(config,state,random);
            const int gray=std::count_if(normal.begin(),normal.end(),[](auto cell) { return cell.is_normal_garbage(); });
            assert(gray>=minimum && gray<=maximum);
            sawNormal[gray]=true;
            const auto timed=Rules::generate_solid_garbage_row(size,Runtime::BoardCell::timed_solid(),
                ModifierMode::Reverse,5,state,random);
            const int releasedGray=std::count_if(timed.begin(),timed.end(),[](auto cell) { return !cell.is_timed_number(); });
            assert(releasedGray>=minimum && releasedGray<=maximum);
            sawTimed[releasedGray]=true;
        }
        // Every integer count in the allowed interval must be reachable.
        for (int count=minimum; count<=maximum; ++count) assert(sawNormal[count] && sawTimed[count]);
    }
}

void garbage_buffer_follows_grown_height() {
    for (int sb:{1,2}) for (int style:{0,1}) for (bool overflowing:{false,true}) {
        UserConfig::Settings user;
        user.qpSnowball=sb;
        Fixture fixture(user);
        auto& context=fixture.context;
        auto& player=fixture.session.player;
        const auto& board=player.gameplay.playfield;
        context.config.simulationPps=1;
        context.config.display.squareStyle=style;
        fixture.session.simulationTime=Runtime::TimePoint::from_seconds(10);
        fixture.session.dynamicRules.incomingGarbageDelay=Runtime::Duration::from_seconds(2.5);
        // Ready, partly elapsed, and newly received packets, in queue order.
        player.garbage.buffer={{2,Runtime::TimePoint::from_seconds(6)},
            {3,Runtime::TimePoint::from_seconds(8)},
            {overflowing ? 30 : 2,Runtime::TimePoint::from_seconds(10)}};
        const auto queue=player.garbage.buffer;
        TerminalGrid screen;
        for (int size=4; size<=20; ++size) {
            screen.draw([&] {
                if (size>4) {
                    Render::erase_player_board(context,player);
                    player.gameplay.scoring.totalLines=(size-4)*20;
                    assert(Engine::grow_snowball_board(context,player));
                }
                Render::redraw_player_board(context,player);
                Engine::GarbageService::redraw_buffer(context,player);
            });
            const int capacity=size+Runtime::kDisplayHiddenRows;
            assert(player.presentation.garbageBufferDisplay.size()==static_cast<std::size_t>(capacity+1));
            assert(player.presentation.garbageBufferDisplay[0]==0);
            const int pending=overflowing ? 35 : 7;
            const int column=player.presentation.margin-3;
            for (int index=1; index<=capacity; ++index) {
                const int state=index>pending ? 0 : index<=2 ? 3 : index<=5 ? 2 : 1;
                assert(player.presentation.garbageBufferDisplay[index]==state);
                const int row=Runtime::kDisplayHiddenRows+size-index;
                const bool top=index==2 || index==5 || (!overflowing && index==7);
                const auto glyph=state ? (top ? U'▇' : U'█') : U' ';
                assert(screen.cells[row][column]==glyph);
                assert(screen.cells[row][column+1]==(state && style==0 ? glyph : U' '));
                if (state) assert(screen.colors[row][column]==
                    (state==3 ? 0xe91600u : state==2 ? 0x9d0f00u : 0xc1a841u));
            }
            assert(screen.cells[Runtime::kDisplayHiddenRows+size][column]==U' ');
            for (std::size_t i=0; i<queue.size(); ++i) {
                assert(player.garbage.buffer[i].line_count()==queue[i].line_count());
                assert(player.garbage.buffer[i].arrival_time()==queue[i].arrival_time());
            }
            assert(board.visibleHeight==size);
        }
        // Clearing the queue after growth must also erase every rendered cell.
        player.garbage.buffer.clear();
        screen.draw([&] { Engine::GarbageService::update_buffer(context,player); });
        for (int row=0; row<Runtime::kDisplayHiddenRows+20; ++row)
            assert(screen.cells[row][player.presentation.margin-3]==U' ');
    }

    // Exercise the real lock/clear growth path, including its redraw call.
    Fixture fixture;
    auto& player=fixture.session.player;
    auto& board=player.gameplay.playfield;
    fixture.context.config.simulationPps=1;
    fixture.session.simulationTime=Runtime::TimePoint::from_seconds(10);
    player.garbage.buffer={{30,Runtime::TimePoint::from_seconds(10)}};
    player.gameplay.scoring.totalLines=19;
    TerminalGrid screen;
    screen.draw([&] {
        Render::redraw_player_board(fixture.context,player);
        Engine::GarbageService::redraw_buffer(fixture.context,player);
    });
    auto piece=Piece::get_definition(Piece::Type::from_index(0));
    auto movement=Engine::make_spawn_movement_state(4);
    while (piece.can_place(movement.pose.translated(1,0),board.field)) ++movement.pose.position.row;
    screen.draw([&] {
        assert(Engine::finalize_locked_piece(fixture.context,board.field,piece,player,movement,0,
            [] { return Rules::PlayResult::ContinueFrame; })==Rules::PlayResult::Locked);
    });
    assert(board.visibleHeight==5 && player.presentation.garbageBufferDisplay.size()==14);
    assert(player.presentation.garbageBufferDisplay[1]==1);
    assert(player.presentation.garbageBufferDisplay[13]==1);
    assert(screen.cells[Runtime::kDisplayHiddenRows+4][player.presentation.margin-3]==U'█');
    assert(screen.cells[0][player.presentation.margin-3]==U'█');
}

void garbage_packet_boundaries_update_without_color_changes() {
    for (int style:{0,1}) {
        Fixture fixture;
        auto& context=fixture.context;
        auto& player=fixture.session.player;
        context.config.simulationPps=1;
        context.config.display.squareStyle=style;
        fixture.session.simulationTime=Runtime::TimePoint::from_seconds(10);
        const auto arrived=Runtime::TimePoint::from_seconds(10);
        const int column=player.presentation.margin-3;
        TerminalGrid screen;
        const auto at=[&](int index) {
            return screen.cells[Runtime::kDisplayHiddenRows+player.gameplay.playfield.visibleHeight-index][column];
        };
        player.garbage.buffer={{3,arrived},{2,arrived}};
        screen.draw([&] { Engine::GarbageService::update_buffer(context,player); });
        assert(at(3)==U'▇' && at(5)==U'▇' && at(2)==U'█');
        player.garbage.buffer={{5,arrived}};
        screen.draw([&] { Engine::GarbageService::update_buffer(context,player); });
        assert(at(3)==U'█' && at(5)==U'▇');
        player.garbage.buffer={{2,arrived},{1,arrived},{2,arrived}};
        screen.draw([&] { Engine::GarbageService::update_buffer(context,player); });
        assert(at(2)==U'▇' && at(3)==U'▇' && at(5)==U'▇');
        // Partial cancellation shortens the first packet and moves all tops.
        player.garbage.buffer.front().consume_lines(1);
        screen.draw([&] { Engine::GarbageService::update_buffer(context,player); });
        assert(at(1)==U'▇' && at(2)==U'▇' && at(3)==U'█' && at(4)==U'▇' && at(5)==U' ');
        TerminalGrid redrawn;
        redrawn.draw([&] { Engine::GarbageService::redraw_buffer(context,player); });
        assert(screen.cells==redrawn.cells);
        for (int row=0;row<160;++row) for (int col=0;col<160;++col)
            if (screen.cells[row][col]!=U' ') assert(screen.colors[row][col]==redrawn.colors[row][col]);
        // A clipped packet has no artificial top boundary at the ceiling.
        player.garbage.buffer={{30,arrived}};
        screen.draw([&] { Engine::GarbageService::update_buffer(context,player); });
        assert(at(4+Runtime::kDisplayHiddenRows)==U'█');
        player.garbage.buffer.clear();
        screen.draw([&] { Engine::GarbageService::redraw_buffer(context,player); });
        for (int index=1;index<=4+Runtime::kDisplayHiddenRows;++index) assert(at(index)==U' ');
    }
}

void countdown_growth_preserves_mods_and_remaining_time() {
    for (int sb:{1,2}) for (int dh:{0,1,2}) for (unsigned seed=0; seed<16; ++seed) {
        UserConfig::Settings user;
        user.qpSnowball=sb; user.qpDoubleHole=dh; user.qpAllSpin=1;
        Fixture fixture(user);
        auto& player=fixture.session.player;
        auto& board=player.gameplay.playfield;
        player.garbage.random.seed(seed);
        Engine::GarbageService::add_solid_garbage(fixture.context,player,board.field,1,Runtime::BoardCell::timed_solid());
        const auto decay=[&] {
            return Engine::decay_timed_solid_garbage(board.width,-Runtime::kHiddenRowCapacity,board.visibleHeight,
                [&](int row,int col) { return board.field.cell(row,col); },
                [&](int row,int col,Runtime::BoardCell cell) { board.field.set_cell(row,col,cell); });
        };
        decay();
        int originalNumbers=0;
        for (int col=0; col<4; ++col) originalNumbers+=board.field.cell(3,col).is_timed_number();
        if (dh==2) assert(originalNumbers>=2 && originalNumbers<=3);
        else assert(originalNumbers>=1 && originalNumbers<=(dh==1 ? 2 : 1));
        for (int size=5; size<=20; ++size) {
            const int offset=(size-4)/2-(size-5)/2;
            const auto oldField=board.field;
            player.gameplay.scoring.totalLines=(size-4)*20;
            assert(Engine::grow_snowball_board(fixture.context,player));
            int numbers=0;
            for (int col=0; col<size; ++col) {
                const auto cell=board.field.cell(size-1,col);
                assert(cell.kind()==Runtime::BoardCellKind::TimedSolid);
                if (cell.is_timed_number()) { ++numbers; assert(cell.timed_countdown()==5); }
                if (col>=offset && col<offset+size-1)
                    assert(cell==oldField.cell(size-2,col-offset));
            }
            if (dh==2) {
                const auto [minimum,maximum]=kReverseGrayBounds[size-4];
                assert(size-numbers>=minimum && size-numbers<=maximum);
            }
            else assert(numbers==originalNumbers);
        }
        for (int i=0; i<4; ++i) assert(decay().releasedRows==0);
        assert(decay().releasedRows==1);
        int gray=0;
        for (int col=0; col<20; ++col) gray+=board.field.cell(19,col).is_normal_garbage();
        if (dh==2) assert(gray>=6 && gray<=8);
        else assert(gray==20-originalNumbers);
    }
}

void random_growth_matches_replay_path() {
    for (int sb:{1,2}) for (bool jump:{false,true}) {
        UserConfig::Settings user;
        user.qpSnowball=sb; user.qpDoubleHole=2; user.qpAllSpin=1;
        Fixture live(user),playback(user);
        playback.context.config.mode.replayMode=true;
        for (auto* fixture:{&live,&playback}) {
            auto& player=fixture->session.player;
            auto& field=player.gameplay.playfield.field;
            Engine::GarbageService::add_garbage(fixture->context,player,field,6);
            Engine::GarbageService::add_solid_garbage(fixture->context,player,field,1,Runtime::BoardCell::timed_solid());
        }
        for (int size=jump ? 20 : 5; size<=20; size+=jump ? 16 : 1) {
            for (auto* fixture:{&live,&playback}) {
                auto& player=fixture->session.player;
                player.gameplay.scoring.totalLines=(size-4)*20;
                assert(Engine::grow_snowball_board(fixture->context,player));
                // Subsequent garbage must consume the same random stream too.
                Engine::GarbageService::add_garbage(fixture->context,player,player.gameplay.playfield.field,1);
            }
            const auto& livePlayer=live.session.player;
            const auto& replayPlayer=playback.session.player;
            assert(livePlayer.garbage.random==replayPlayer.garbage.random);
            assert(livePlayer.pieces.random==replayPlayer.pieces.random);
            assert(livePlayer.garbage.holeOrder==replayPlayer.garbage.holeOrder);
            for (int row=-Runtime::kHiddenRowCapacity; row<size; ++row)
                for (int col=0; col<size; ++col)
                    assert(livePlayer.gameplay.playfield.field.cell(row,col)==replayPlayer.gameplay.playfield.field.cell(row,col));
            // Check the actual engine path for both gradual and direct growth.
            const auto [minimum,maximum]=kReverseGrayBounds[size-4];
            for (int row=size-3; row<size; ++row) {
                int gray=0;
                for (int col=0; col<size; ++col) {
                    const auto cell=livePlayer.gameplay.playfield.field.cell(row,col);
                    gray+=cell.is_normal_garbage() ||
                        (cell.kind()==Runtime::BoardCellKind::TimedSolid && !cell.is_timed_number());
                }
                assert(gray>=minimum && gray<=maximum);
            }
        }
    }
}

void wide_pieces_and_clutch() {
    Fixture fixture;
    auto& player=fixture.session.player;
    auto& board=player.gameplay.playfield;
    auto i5=Piece::get_definition(Piece::Type::from_index(21));
    player.clutch.previousPieceCleared=true;
    player.clutch.consecutive=3;
    auto spawn=Engine::make_spawn_movement_state(4);
    assert(Engine::handle_spawn_collision(fixture.context,board.field,i5,player,spawn));
    assert(spawn.pose.position.row==-2 && player.clutch.consecutive==3 && !player.clutch.currentTurnLifted);
    for (const auto& entry:Piece::catalog()) {
        auto piece=Piece::get_definition(entry.type);
        int minimum=100,maximum=-100;
        for (const auto& cell:piece.cells(Runtime::Rotation{})) {
            minimum=std::min(minimum,cell[1]); maximum=std::max(maximum,cell[1]);
        }
        board.field.clear(); player.clutch={true,false,0};
        auto state=Engine::make_spawn_movement_state(4);
        const bool dead=Engine::handle_spawn_collision(fixture.context,board.field,piece,player,state);
        if (maximum-minimum+1>4) {
            assert(dead && !player.clutch.currentTurnLifted && player.clutch.consecutive==0);
        } else {
            assert(!dead && piece.can_place(state.pose,board.field));
            assert(state.pose.position.col+minimum>=0 && state.pose.position.col+maximum<4);
        }
    }
    board.field.clear(); player.clutch={true,false,0};
    player.gameplay.scoring.totalLines=20;
    Engine::grow_snowball_board(fixture.context,player);
    spawn=Engine::make_spawn_movement_state(5);
    assert(!Engine::handle_spawn_collision(fixture.context,board.field,i5,player,spawn));
    assert(!player.clutch.currentTurnLifted);
    const auto cell=i5.cells(spawn.pose.rotation).front();
    board.field.set_cell(spawn.pose.position.row+cell[0],spawn.pose.position.col+cell[1],Runtime::BoardCell::piece(1));
    assert(!Engine::handle_spawn_collision(fixture.context,board.field,i5,player,spawn));
    assert(player.clutch.currentTurnLifted && player.clutch.consecutive==1);
    assert(i5.can_place(spawn.pose,board.field));
    // A completely sealed visible board has no playable area to recover into.
    Engine::GarbageService::add_solid_garbage(fixture.context,player,board.field,5);
    assert(board.activeHeight==0);
    auto standard=Piece::get_definition(Piece::Type::from_index(0));
    spawn=Engine::make_spawn_movement_state(5);
    assert(Engine::handle_spawn_collision(fixture.context,board.field,standard,player,spawn));
    assert(player.clutch.consecutive==1);
}

void settings_and_reverse_rules() {
    UserConfig::Settings user;
    const auto* descriptor=UserConfig::find_setting(UserConfig::Section::QuickPlay,"Snowball");
    assert(descriptor && descriptor->id==UserConfig::SettingId::QpSnowball);
    assert(descriptor->label=="Snowball");
    assert(UserConfig::assign_setting_text(user,*descriptor,"2"));
    assert(user.qpSnowball==2);
    user.persistencePath="snowball_test.ini";
    assert(UserConfig::save(user));
    UserConfig::Settings loaded;
    UserConfig::load(loaded,user.persistencePath);
    assert(loaded.qpSnowball==2);
    std::filesystem::remove(user.persistencePath);
    Runtime::RuntimeConfig decoded;
    Replay::ConfigPrefixResult prefix;
    for (int mode=0; mode<3; ++mode) {
        user.qpSnowball=mode;
        auto config=Runtime::normalize_user_config(user,Runtime::GameMode::QuickPlay);
        assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",decoded,prefix));
        assert(decoded.quickPlay.snowball==config.quickPlay.snowball);
    }
    decoded.quickPlay.snowball=ModifierMode::Reverse;
    assert(Replay::decode_config_prefix("{{Model,3}}",decoded,prefix));
    assert(decoded.quickPlay.snowball==ModifierMode::Disabled);
    Runtime::QuickPlayConfig mods;
    mods.snowball=ModifierMode::Enabled;
    assert(!Rules::qp_hard_mode(mods));
    mods.snowball=ModifierMode::Reverse;
    assert(Rules::qp_hard_mode(mods));
    assert(Rules::qp_garbage_delay(mods,1,0).seconds()==2.5);
    mods.expert=ModifierMode::Reverse;
    assert(std::fabs(Rules::qp_garbage_delay(mods,1,100).seconds()-2.2)<1e-12);
    Rules::AttackInput attack;
    attack.garbageMode=Runtime::GarbageMode::QuickPlay;
    attack.linesCleared=1; attack.combo=1;
    attack.qp.snowball=ModifierMode::Enabled;
    assert(Rules::calculate_attack(attack).attack==1);
    attack.qp.snowball=ModifierMode::Reverse;
    assert(Rules::calculate_attack(attack).attack==0);
}

void actual_locks_and_replay_growth() {
    Fixture live;
    auto& player=live.session.player;
    const auto initialConfig=live.context.config;
    auto piece=Piece::get_definition(Piece::Type::from_index(0));
    for (int turn=0; turn<20; ++turn) {
        auto movement=Engine::make_spawn_movement_state(player.gameplay.playfield.width);
        while (piece.can_place(movement.pose.translated(1,0),player.gameplay.playfield.field))
            ++movement.pose.position.row;
        assert(Engine::finalize_locked_piece(live.context,player.gameplay.playfield.field,piece,
            player,movement,0,[] { return Rules::PlayResult::ContinueFrame; })==Rules::PlayResult::Locked);
        assert(player.gameplay.scoring.totalLines==turn+1);
        assert(player.gameplay.playfield.width==(turn==19 ? 5 : 4));
    }
    Fixture playback;
    playback.context.config=initialConfig;
    playback.context.config.mode.replayMode=true;
    playback.replay.operations=Replay::decode_operation_stream(live.replay.data,0,1);
    Replay::configure_playback(playback.context,1,Runtime::Duration::from_seconds(100));
    Replay::start_playback(playback.context,playback.context.now());
    playback.replay.playback.elapsed=Runtime::Duration::from_seconds(100);
    playback.replay.playback.progressVisible=false;
    auto& replayPlayer=playback.session.player;
    for (int turn=0; turn<20; ++turn) {
        auto movement=Engine::make_spawn_movement_state(replayPlayer.gameplay.playfield.width);
        while (piece.can_place(movement.pose.translated(1,0),replayPlayer.gameplay.playfield.field))
            ++movement.pose.position.row;
        assert(Engine::finalize_locked_piece(playback.context,replayPlayer.gameplay.playfield.field,piece,
            replayPlayer,movement,0,[] { return Rules::PlayResult::ContinueFrame; })==Rules::PlayResult::Locked);
        assert(replayPlayer.gameplay.scoring.totalLines==turn+1);
        assert(replayPlayer.gameplay.playfield.width==(turn==19 ? 5 : 4));
    }
    assert(playback.replay.operations.empty());
    assert(playback.context.config.field.width==5 && playback.context.config.field.height==5);
    assert(replayPlayer.clutch.previousPieceCleared);
}

void multi_line_growth_and_reverse_spin_death() {
    Fixture fixture;
    auto& player=fixture.session.player;
    auto& board=player.gameplay.playfield;
    auto piece=Piece::get_definition(Piece::Type::from_index(3)); // O
    auto movement=Engine::make_spawn_movement_state(4);
    int maximumRow=-100;
    for (const auto& cell:piece.cells(movement.pose.rotation)) maximumRow=std::max(maximumRow,cell[0]);
    movement.pose.position.row=3-maximumRow;
    for (int row=2; row<4; ++row)
        for (int col=0; col<4; ++col) board.field.set_cell(row,col,Runtime::BoardCell::piece(2));
    for (const auto& cell:piece.cells(movement.pose.rotation))
        board.field.set_cell(movement.pose.position.row+cell[0],movement.pose.position.col+cell[1],Runtime::BoardCell::empty());
    board.field.set_cell(0,0,Runtime::BoardCell::piece(1));
    player.gameplay.scoring.totalLines=19;
    assert(Engine::finalize_locked_piece(fixture.context,board.field,piece,player,movement,0,
        [] { return Rules::PlayResult::ContinueFrame; })==Rules::PlayResult::Locked);
    assert(player.gameplay.scoring.totalLines==21 && board.width==5);
    // Collapse by two rows first, then translate by the newly added top row.
    assert(board.field.cell(3,0)==Runtime::BoardCell::piece(1));
    assert(board.field.cell(3,4).empty_cell());

    UserConfig::Settings user;
    user.qpAllSpin=2;
    Fixture reverse(user);
    auto& reversePlayer=reverse.session.player;
    auto& reverseBoard=reversePlayer.gameplay.playfield;
    auto i=Piece::get_definition(Piece::Type::from_index(0));
    for (int turn=0; turn<2; ++turn) {
        auto state=Engine::make_spawn_movement_state(4);
        while (i.can_place(state.pose.translated(1,0),reverseBoard.field)) ++state.pose.position.row;
        const auto result=Engine::finalize_locked_piece(reverse.context,reverseBoard.field,i,reversePlayer,state,0,
            [] { return Rules::PlayResult::ContinueFrame; });
        assert(result==(turn ? Rules::PlayResult::Dead : Rules::PlayResult::Locked));
    }
    assert(reverseBoard.activeHeight==0);
    assert(reversePlayer.clutch.previousPieceCleared);
}

void checkpoints_restore_dimensions() {
    Fixture fixture;
    fixture.context.config.mode.replayMode=true;
    Replay::configure_playback_checkpoint_cache(fixture.context,"snowball");
    Replay::configure_playback(fixture.context,1,Runtime::Duration::from_seconds(60));
    Replay::start_playback(fixture.context,fixture.context.now());
    Replay::capture_playback_checkpoint(fixture.context);
    auto& player=fixture.session.player;
    player.gameplay.scoring.totalLines=40;
    Engine::grow_snowball_board(fixture.context,player);
    assert(fixture.context.config.field.width==6);
    Replay::schedule_playback_checkpoint_restore(fixture.context,Runtime::Duration{});
    assert(Replay::restore_scheduled_playback_checkpoint(fixture.context));
    assert(fixture.context.config.field.width==4 && fixture.context.config.field.height==4);
    assert(player.gameplay.playfield.field.width()==4 && player.gameplay.playfield.field.height()==4);
    player.gameplay.scoring.totalLines=20;
    assert(Engine::grow_snowball_board(fixture.context,player));
    assert(player.gameplay.playfield.width==5);
}

void altitude_preserves_small_board_panels() {
    for (int width:{4,5,6,10}) for (int pieceHeight:{2,4,8})
        for (int nextCount:{0,1,5}) for (bool hold:{false,true}) {
            Fixture fixture;
            auto& player=fixture.session.player;
            auto& board=player.gameplay.playfield;
            player.gameplay.scoring.totalLines=(width-4)*20;
            Engine::grow_snowball_board(fixture.context,player);
            player.presentation.blockMaxHeight=pieceHeight;
            player.presentation.blockMaxWidth=pieceHeight==2 ? 3 : pieceHeight;
            board.nextCount=nextCount;
            board.holdEnabled=hold;
            if (hold) player.gameplay.hold.piece=Piece::Type::from_index(0);
            player.gameplay.quickPlay.blighted=true;
            TerminalGrid screen;
            screen.draw([&] {
                Render::print_border(fixture.context,player);
                if (hold) Render::draw_board_hold(fixture.context,player);
                for (int i=0; i<nextCount; ++i) Render::draw_board_next(fixture.context,player,i);
                Render::print_message(fixture.context,player);
                Render::print_blighted(fixture.context,player);
            });
            const auto panels=screen.cells;
            const int panelBottom=Runtime::kDisplayHiddenRows+std::max(
                player.presentation.hold_panel_height()+15,
                player.presentation.next_panel_height(nextCount)+1);
            for (int update=0; update<4; ++update) {
                fixture.session.quickPlay.speed=update==1 ? 10 : update==2 ? 2 : 1;
                fixture.session.quickPlay.altitude=update==1 ? 98765.4 : 0;
                fixture.session.quickPlay.progress.windupUntil=update==1 ? 1e20 : 0;
                screen.draw([&] { Render::print_altitude(fixture.context,player); });
                for (int row=0; row<=panelBottom; ++row) for (int column=0; column<160; ++column) {
                    if (column>=player.presentation.margin-1 &&
                        column<player.presentation.margin+board.width*2) continue;
                    assert(screen.cells[row][column]==panels[row][column]);
                }
            }
            // The short altitude remains at the same footer, while every
            // leftover long-altitude character and warning disappears.
            const std::u32string_view shortAltitude=U"- 0.0 -";
            const auto position=screen.find_text(shortAltitude);
            assert(position.first==panelBottom+1);
            assert(position.second+4==player.presentation.margin+board.width);
            for (int row=panelBottom+1; row<160; ++row) for (int col=0; col<160; ++col) {
                if (row==position.first && col>=position.second && col<position.second+7) continue;
                assert(screen.cells[row][col]==U' ');
            }
        }

    // Growth must preserve both the new bottom border and the fixed footer.
    Fixture fixture;
    auto& player=fixture.session.player;
    TerminalGrid screen;
    screen.draw([&] { Render::redraw_player_board(fixture.context,player); });
    screen.draw([&] {
        Render::erase_player_board(fixture.context,player);
        player.gameplay.scoring.totalLines=20;
        assert(Engine::grow_snowball_board(fixture.context,player));
        Render::redraw_player_board(fixture.context,player);
    });
    const int borderRow=Runtime::kDisplayHiddenRows+5;
    const int borderColumn=player.presentation.margin-1;
    assert(screen.cells[borderRow][borderColumn]==U'+');
    for (int column=1; column<=10; ++column) assert(screen.cells[borderRow][borderColumn+column]==U'-');
    assert(screen.cells[borderRow][borderColumn+11]==U'|'); // shared NEXT border

    fixture.session.quickPlay.speed=10;
    fixture.session.quickPlay.altitude=98765.4;
    fixture.session.quickPlay.progress.windupUntil=1e20;
    screen.draw([&] { Render::print_altitude(fixture.context,player); });
    const int oldFooter=Render::board_footer_base_row(player)+Runtime::kDisplayHiddenRows;
    screen.draw([&] {
        Render::erase_player_board(fixture.context,player);
        player.gameplay.scoring.totalLines=40;
        assert(Engine::grow_snowball_board(fixture.context,player));
        fixture.session.quickPlay.speed=1;
        fixture.session.quickPlay.altitude=0;
        fixture.session.quickPlay.progress.windupUntil=0;
        Render::redraw_player_board(fixture.context,player);
    });
    const auto shortPosition=screen.find_text(U"- 0.0 -");
    assert(shortPosition.first==oldFooter+1 &&
        shortPosition.second+4==player.presentation.margin+player.gameplay.playfield.width);
    for (int row=oldFooter+1; row<=oldFooter+3; ++row) for (int col=0; col<160; ++col) {
        if (row==shortPosition.first && col>=shortPosition.second && col<shortPosition.second+7) continue;
        assert(screen.cells[row][col]==U' ');
    }

    fixture.context.config.mode.replayMode=true;
    Replay::configure_playback(fixture.context,1,Runtime::Duration::from_seconds(60));
    Replay::start_playback(fixture.context,fixture.context.now());
    fixture.session.quickPlay.speed=10;
    fixture.session.quickPlay.altitude=98765.4;
    fixture.session.quickPlay.progress.windupUntil=1e20;
    screen.draw([&] { Render::print_altitude(fixture.context,player); });
    const auto beforeTimeline=screen.cells;
    screen.draw([&] { Engine::render_replay_playback_progress(fixture.context,player); });
    for (int row=0; row<=oldFooter+3; ++row) assert(screen.cells[row]==beforeTimeline[row]);
}

void snowball_altitude_stays_relative_to_layout() {
    for (int mode:{1,2}) for (int fool:{0,4}) for (int noHold:{0,2}) {
        UserConfig::Settings user;
        user.qpSnowball=mode; user.qpFool=fool; user.qpNoHold=noHold;
        Fixture fixture(user);
        auto& player=fixture.session.player;
        const auto& board=player.gameplay.playfield;
        fixture.context.config.mode.replayMode=true;
        Replay::configure_playback(fixture.context,1,Runtime::Duration::from_seconds(60));
        Replay::start_playback(fixture.context,fixture.context.now());
        TerminalGrid screen;
        for (int size=4; size<=20; ++size) {
            if (size>4) screen.draw([&] {
                Render::erase_player_board(fixture.context,player);
                player.gameplay.scoring.totalLines=(size-4)*20;
                assert(Engine::grow_snowball_board(fixture.context,player));
            });
            screen.draw([&] { Render::redraw_player_board(fixture.context,player); });
            const int footer=Runtime::kDisplayHiddenRows+std::max({board.visibleHeight,
                player.presentation.hold_panel_height()+15,
                player.presentation.next_panel_height(board.nextCount)+1});
            const int center=player.presentation.margin+board.width;
            for (bool longText:{false,true,false}) {
                fixture.session.quickPlay.speed=longText ? 10 : 1;
                fixture.session.quickPlay.altitude=longText ? 98765.4 : 0;
                fixture.session.quickPlay.progress.windupUntil=longText ? 1e20 : 0;
                screen.draw([&] {
                    Render::print_altitude(fixture.context,player);
                    Engine::render_replay_playback_progress(fixture.context,player);
                });
                const std::u32string_view text=longText ? U"===== 98765.4 =====" : U"- 0.0 -";
                const auto position=screen.find_text(text);
                assert(position.first==footer+1);
                assert(position.second+static_cast<int>((text.size()+1)/2)==center);
                assert(screen.cells[footer+3][center-1]==(longText ? U'!' : U' '));
            }
        }
    }
}
}

int main() {
    Capture output;
    combinations_start_empty(); growth_preserves_state_and_stops();
    solid_and_countdown_rows_remain_barriers(); wide_pieces_and_clutch();
    garbage_buffer_follows_grown_height(); garbage_rows_follow_mods_when_growing();
    garbage_packet_boundaries_update_without_color_changes();
    reverse_garbage_density_at_all_widths();
    countdown_growth_preserves_mods_and_remaining_time(); random_growth_matches_replay_path();
    settings_and_reverse_rules(); actual_locks_and_replay_growth();
    multi_line_growth_and_reverse_spin_death(); checkpoints_restore_dimensions();
    altitude_preserves_small_board_panels();
    snowball_altitude_stays_relative_to_layout();
    std::cerr << "snowball: OK\n";
}
