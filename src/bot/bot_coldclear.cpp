// C++ adaptation of MinusKelvin/cold-clear. SPDX-License-Identifier: MPL-2.0
#include "tetr/bot/bot_coldclear.h"
#include "tetr/bot/quick_play_search.h"
#include "coldclear/core.h"
#include <algorithm>
#include <chrono>

namespace Bot_coldclear {
namespace {
thread_local Diagnostics diagnostics;
double qp_terrain(const std::vector<std::uint32_t>& rows,int width,int height,int,int b2b) {
    if(width==10) {
        ColdClear::Board board;
        for(std::size_t i=0;i<rows.size();++i)if(i<board.rows.size())board.rows[i]=rows[i];
            else if(rows[i])return -1e12;
        return ColdClear::terrain(board,b2b,0);
    }
    // Standard's height, cavity, overhang, well and surface terms on variable
    // fields. T-slot projections and ten-column preferences stay native-only.
    std::vector<int> heights(width);int highest=0,holes=0,covered=0,coveredSq=0,bump=0,bumpSq=0;
    for(int x=0;x<width;++x) {
        for(int y=static_cast<int>(rows.size())-1;y>=0;--y)if(rows[y]&(std::uint32_t{1}<<x)){heights[x]=y+1;break;}
        highest=std::max(highest,heights[x]);
        for(int y=0;y<heights[x];++y)if(!(rows[y]&(std::uint32_t{1}<<x))) {
            ++holes;const int cover=std::min(6,heights[x]-y-1);covered+=cover;coveredSq+=cover*cover;
        }
    }
    const int well=std::min_element(heights.begin(),heights.end())-heights.begin();
    int depth=0,previous=-1;
    for(int y=heights[well];y<highest;++y) {
        bool full=true;for(int x=0;x<width;++x)if(x!=well && !(rows[y]&(std::uint32_t{1}<<x)))full=false;
        if(!full)break;++depth;
    }
    for(int x=0;x<width;++x)if(x!=well) {
        if(previous>=0){const int d=std::abs(heights[x]-heights[previous]);bump+=d;bumpSq+=d*d;}previous=x;
    }
    return (b2b?52:0)-39*highest-150*std::max(0,highest-height/2)-511*std::max(0,highest-height+5)+
        57*std::min(depth,17)-24*bump-7*bumpSq-173*holes-3*holes*holes-17*covered-coveredSq;
}
}
Diagnostics last_diagnostics() { return diagnostics; }
bool init(const Bot::Environment& environment) { diagnostics = {}; return bool(availability(environment)); }

int getpos(const Bot::Environment& environment, const Runtime::PlayerSessionState& player,
           Runtime::Playfield field, std::vector<int> next, const std::vector<int>& unseenBagPieces,
           int startRow, int startCol, int startRotation, int hold,
           int& answerRow, int& answerCol, int& answerRotation, int& answerKick) {
    diagnostics = {};
    const auto began = std::chrono::steady_clock::now();
    if (!availability(environment) || next.empty()) return -1;
    if(Bot::QuickPlaySearch::enabled(environment)) {
        using namespace Bot::QuickPlaySearch;
        const auto native=[&](const NativeBaseline& baseline) {
            Decision result;if(!availability(baseline.environment))return result;
            int row=startRow+baseline.rowOffset,col=startCol,r=startRotation,k=0;
            result.selected=getpos(baseline.environment,*baseline.player,*baseline.field,next,unseenBagPieces,
                row,col,r,hold,row,col,r,k);
            result.pose={{row-baseline.rowOffset,col},Runtime::Rotation::from_index(r)};
            result.rotation=k>0?Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(k-1)):Runtime::RotationRecord{};
            diagnostics.target=result.pose;return result;
        };
        const auto result=adapt(Profile::ColdClear,environment,player,field,next,unseenBagPieces,
            {{startRow,startCol},Runtime::Rotation::from_index(startRotation)},hold,native,qp_terrain);
        answerRow=result.pose.position.row;answerCol=result.pose.position.col;answerRotation=result.pose.rotation.index();
        answerKick=result.rotation.rotated()?result.rotation.kick().index()+1:0;
        if(!result.usedNative) {
            diagnostics={};diagnostics.selected=result.selected;diagnostics.target=result.pose;diagnostics.rotation=result.rotation;
            diagnostics.depth=result.depth;diagnostics.fallback=result.selected<0;
            diagnostics.usedHold=result.selected>=0 && result.selected!=next.front();
            diagnostics.projectedRise=result.risen;
            if(result.selected>=0) {
                const auto type=result.selected<7?environment.pieceTypes[result.selected]:Piece::Type::from_index(result.selected);
                const auto forecast=forecast_placement(environment,player,field,type,result.pose,result.rotation);
                diagnostics.attack=forecast.placement.attack.attack;diagnostics.clearedLines=forecast.placement.clear.clearedLines;
                diagnostics.spin=forecast.placement.spin.spin;diagnostics.mini=forecast.placement.attack.effectiveMini;
            }
        }
        diagnostics.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
        return result.selected;
    }
    for (int piece : next) if (piece < 0 || piece >= 7) return -1;
    if (hold >= 7) return -1;
    ColdClear::Input input;
    input.environment = environment;
    input.next = std::move(next);
    input.start = {{startRow,startCol},Runtime::Rotation::from_index(startRotation)};
    input.canHold = environment.holdEnabled && hold != -2;
    for (int piece = 0; piece < 7; ++piece) {
        input.pieces[piece] = Piece::get_definition(environment.pieceTypes[piece]);
        input.metadata[piece] = Piece::metadata(environment.pieceTypes[piece]);
    }
    for (int row = -Runtime::kHiddenRowCapacity; row < ColdClear::Height; ++row)
        for (int col = 0; col < ColdClear::Width; ++col)
            if (field.cell(row,col).occupied()) {
                input.root.board.rows[ColdClear::Height-1-row] |= 1u<<col;
                if (field.cell(row,col).is_piece()) input.root.board.pieceRows[ColdClear::Height-1-row] |= 1u<<col;
            }
    input.root.hold = hold >= 0 ? hold : -1;
    input.root.b2b = player.gameplay.scoring.b2b;
    input.root.combo = player.gameplay.scoring.combo;
    input.root.garbageHeight = player.gameplay.playfield.garbageHeight;
    input.root.previousCleared = player.clutch.previousPieceCleared;
    input.root.placed = player.gameplay.scoring.placedBlocks;
    input.root.sent = environment.sentAttack;
    input.root.time = environment.nowSeconds;
    input.root.incoming = player.garbage.buffer;
    input.root.cancellation = player.garbage.cancellation;
    input.root.bag = 0;
    for (int piece : unseenBagPieces) if (piece >= 0 && piece < 7) input.root.bag |= 1u<<piece;
    if (!input.root.bag || environment.randomPieces) input.root.bag = 127;
    const auto chosen = ColdClear::search(input,diagnostics);
    if (chosen.move.piece < 0) {
        diagnostics.fallback = true;
        // A grounded reachable answer is still returned when deeper search
        // proves every branch dead. The host applies its own last legality check.
        const auto moves = ColdClear::find_moves(input,input.root,input.next.front(),input.start,false);
        if (moves.empty()) return -1;
        answerRow = moves.front().pose.position.row; answerCol = moves.front().pose.position.col;
        answerRotation = moves.front().pose.rotation.index(); answerKick = 0;
    } else {
        answerRow = chosen.move.pose.position.row; answerCol = chosen.move.pose.position.col;
        answerRotation = chosen.move.pose.rotation.index();
        answerKick = chosen.move.rotation.rotated() ? std::min(chosen.move.rotation.kick().index()+1,6) : 0;
        diagnostics.attack = chosen.lock.attack.attack; diagnostics.clearedLines = chosen.lock.clear.clearedLines;
        diagnostics.spin = chosen.lock.spin.spin; diagnostics.mini = chosen.lock.attack.effectiveMini;
        diagnostics.usedHold = chosen.move.hold; diagnostics.projectedRise = chosen.state.rise;
        diagnostics.target = chosen.move.pose; diagnostics.rotation = chosen.move.rotation; diagnostics.selected = chosen.move.piece;
    }
    diagnostics.milliseconds = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
    return chosen.move.piece >= 0 ? chosen.move.piece : input.next.front();
}
}
