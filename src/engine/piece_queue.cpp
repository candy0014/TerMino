#include "tetr/engine/piece_queue.h"
#include "tetr/util/deterministic_random.h"

#include "tetr/runtime/piece.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <limits>

namespace Engine {

Runtime::PieceQueueState& piece_queue(Runtime::PlayerSessionState& player){ return player.pieces; }
const Runtime::PieceQueueState& piece_queue(const Runtime::PlayerSessionState& player){ return player.pieces; }
bool finite_map_sequence(const Runtime::GameContext& context) {
    return context.config.challengeRules!=Runtime::ChallengeRules::ConstructMap && context.config.initialMap && context.config.initialMap->generator==Runtime::MapGenerator::CustomSequence;
}
bool hold_replacement_available(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player) {
    return !finite_map_sequence(context) || player.pieces.bag.size()>1 || player.gameplay.hold.piece.valid();
}

namespace {
void extend_fool_bag(Runtime::GameContext& context, Runtime::PlayerSessionState& player, std::vector<Piece::Type>& bag) {
    if(context.config.initialMap) return;
    auto& pieces=player.pieces;
    const int floor=context.session.quickPlay.floor;
    if(pieces.qpBagBreather) pieces.qpBagBreather=false;
    else {
        if(pieces.bagCount%2==0) bag.push_back(Piece::april_fools_piece(static_cast<int>(pieces.random()%(std::min(3,std::max(1,floor-1))*7)),context.config.legacyEventGeometry));
        if(pieces.foolLastTypes.empty()) {pieces.foolLastTypes.resize(7);std::iota(pieces.foolLastTypes.begin(),pieces.foolLastTypes.end(),0);}
        Util::deterministic_shuffle(pieces.foolLastTypes,pieces.random);
        bag.push_back(Piece::april_fools_piece(pieces.foolLastTypes.back()+(floor-1)*7,context.config.legacyEventGeometry));
        pieces.foolLastTypes.pop_back();
    }
}
void extend_penalty_bag(Runtime::GameContext& context, Runtime::PlayerSessionState& player, std::vector<Piece::Type>& bag) {
    if(context.config.initialMap) return;
    if(context.config.garbage.mode!=Runtime::GarbageMode::QuickPlay || !context.config.quickPlay.continuousCancellation) return;
    auto& pieces=player.pieces;
    const double penalty=context.session.quickPlay.progress.cancellation;
    const auto extra=[&](int family) {
        for(const auto piece:pieces.allowedPieces) if(Piece::metadata(piece).familyIndex==family) {bag.push_back(piece);return;}
    };
    if(penalty>=20) extra(3);
    if(penalty>=30) extra(pieces.random()%2 ? 1 : 2);
    if(penalty>=40) extra(pieces.random()%2 ? 4 : 6);
    if(penalty>=40 && !context.session.quickPlay.progress.extraI5Issued && context.config.quickPlay.fool==Runtime::FoolMode::Disabled) {
        bag.push_back(Piece::Type::from_index(21));context.session.quickPlay.progress.extraI5Issued=true;
    }
    if(penalty>=50) {extra(pieces.random()%2 ? 1 : 2);extra(pieces.random()%2 ? 0 : 5);}
    if(penalty>=60) extra(pieces.random()%2 ? 4 : 6);
}
void add_legacy_bag(Runtime::GameContext& context, Runtime::PlayerSessionState& player, bool random) {
    auto& pieces=player.pieces;
    const auto fool=context.config.quickPlay.fool;
    const bool april=fool==Runtime::FoolMode::AprilFools2026 || fool==Runtime::FoolMode::ReverseAprilFools2026;
    const int floor=context.session.quickPlay.floor;
    if(random) {
        if(april) {
            const int roll=pieces.random()%17;
            if(roll<14) pieces.bag.push_back({pieces.allowedPieces[pieces.random()%pieces.allowedPieces.size()],true});
            else if(roll<16) pieces.bag.push_back({Piece::april_fools_piece(static_cast<int>(pieces.random()%7)+(floor-1)*7,context.config.legacyEventGeometry),true});
            else pieces.bag.push_back({Piece::april_fools_piece(static_cast<int>(pieces.random()%(std::min(3,std::max(1,floor-1))*7)),context.config.legacyEventGeometry),true});
        } else pieces.bag.push_back({pieces.allowedPieces[pieces.random()%pieces.allowedPieces.size()],true});
        return;
    }
    auto bag=pieces.allowedPieces;
    if(april) extend_fool_bag(context,player,bag);
    else extend_penalty_bag(context,player,bag);
    Util::deterministic_shuffle(bag,pieces.random);
    for(std::size_t i=0;i<bag.size();++i) pieces.bag.push_back({bag[i],i==0});
}

void append_generated_bag(Runtime::PlayerSessionState& player, const std::vector<int>& indices) {
    for(std::size_t i=0;i<indices.size();++i)
        player.pieces.bag.push_back({player.pieces.allowedPieces.at(indices[i]),i==0});
}
} // namespace

void add_bag(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    if(finite_map_sequence(context)) return;
    auto& pieces=player.pieces;
    if(pieces.allowedPieces.empty()) throw std::logic_error("cannot generate a bag from an empty piece set");
    ++pieces.bagCount;
    const auto mode=Runtime::effective_bag_randomizer(context.config);
    if(mode==Runtime::BagRandomizer::Drought) {
        std::vector<Piece::Type> bag;
        for(auto type:pieces.allowedPieces)if(Piece::metadata(type).familyIndex!=0)bag.push_back(type);
        if(bag.empty())throw std::logic_error("drought requires non-I pieces");
        Util::deterministic_shuffle(bag,pieces.random);
        for(std::size_t i=0;i<bag.size();++i)pieces.bag.push_back({bag[i],i==0});
        return;
    }
    if(mode==Runtime::BagRandomizer::Adversarial) {
        // For each piece find its best simple drop; supply the piece whose
        // best resulting terrain is worst. Recompute as the board changes.
        const auto& field=player.gameplay.playfield.field;
        auto order=pieces.allowedPieces;Util::deterministic_shuffle(order,pieces.random);
        Piece::Type worst=order.front();double worstValue=-std::numeric_limits<double>::infinity();
        for(auto type:order) {
            const auto piece=Piece::get_definition(type);double best=1e12;
            if(field.initialized())for(int rotation=0;rotation<4;++rotation)for(int col=-4;col<field.width()+4;++col) {
                auto pose=Piece::spawn_pose(field.width(),type);pose.rotation=Runtime::Rotation(rotation);pose.position.col=col;
                if(!piece.can_place(pose,field))continue;
                while(piece.can_place(pose.translated(1,0),field))++pose.position.row;
                const auto occupied=[&](int y,int x){
                    if(field.cell(y,x).occupied())return true;
                    for(const auto& cell:piece.cells(pose.rotation))if(pose.position.row+cell[0]==y && pose.position.col+cell[1]==x)return true;
                    return false;
                };
                int lines=0,holes=0,height=0,bump=0,last=0;
                for(int y=0;y<field.height();++y){bool full=true;for(int x=0;x<field.width();++x)full=full && occupied(y,x);lines+=full;}
                for(int x=0;x<field.width();++x){bool covered=false;int column=0;for(int y=0;y<field.height();++y){if(occupied(y,x)){if(!covered)column=field.height()-y;covered=true;}else if(covered)++holes;}height+=column;if(x)bump+=std::abs(column-last);last=column;}
                best=std::min(best,height+8.0*holes+.5*bump-10.0*lines);
            }
            if(best>worstValue){worstValue=best;worst=type;}
        }
        pieces.bag.push_back({worst,true});return;
    }
    if(pieces.tetrioRandom) {
        if(context.config.legacyRandomizerRules) {
            const auto bag=pieces.tetrioRandom->bag();
            append_generated_bag(player,std::vector<int>(bag.begin(),bag.end()));
            return;
        }
        std::vector<int> order(pieces.allowedPieces.size());
        std::iota(order.begin(),order.end(),0);
        if(order.size()==7) order={6,2,3,4,0,1,5}; // Official Z L O S I J T order.
        const auto choose=[&](int bound){return static_cast<int>(pieces.tetrioRandom->uniform()*bound);};
        const auto shuffle=[&](std::vector<int>& values) {
            for(int i=static_cast<int>(values.size())-1;i>0;--i) std::swap(values[i],values[choose(i+1)]);
        };
        append_generated_bag(player,Runtime::generate_piece_bag(mode,order,pieces.bagCount-1,pieces.randomizerState,choose,shuffle));
        return;
    }
    const bool fool=context.config.quickPlay.fool==Runtime::FoolMode::AprilFools2026 ||
        context.config.quickPlay.fool==Runtime::FoolMode::ReverseAprilFools2026;
    // Keep historical seeds, event-piece composition, and old replay RNG use.
    if(context.config.legacyRandomizerRules || mode==Runtime::BagRandomizer::SevenBag ||
        (mode==Runtime::BagRandomizer::TotalMayhem && fool)) {
        add_legacy_bag(context,player,context.config.legacyRandomizerRules ? context.config.randomPieces : mode==Runtime::BagRandomizer::TotalMayhem);
        return;
    }
    std::vector<int> order(pieces.allowedPieces.size());
    std::iota(order.begin(),order.end(),0);
    const auto choose=[&](int bound) {
        return static_cast<int>(static_cast<double>(pieces.random())/(static_cast<double>(std::mt19937::max())+1.0)*bound);
    };
    const auto shuffle=[&](std::vector<int>& values){Util::deterministic_shuffle(values,pieces.random);};
    const auto indices=Runtime::generate_piece_bag(mode,order,pieces.bagCount-1,pieces.randomizerState,choose,shuffle);
    std::vector<Piece::Type> generated;
    for(const auto index:indices) generated.push_back(pieces.allowedPieces.at(index));
    const auto before=generated.size();
    if(fool) extend_fool_bag(context,player,generated);
    else if(mode!=Runtime::BagRandomizer::TotalMayhem) extend_penalty_bag(context,player,generated);
    if(generated.size()!=before) Util::deterministic_shuffle(generated,pieces.random);
    for(std::size_t i=0;i<generated.size();++i) pieces.bag.push_back({generated[i],i==0});
}




Piece::Type current_piece(const Runtime::PlayerSessionState& player){
    if (player.pieces.bag.empty()) throw std::logic_error("cannot read current piece from an empty queue");
    return player.pieces.bag.front().type;
}
void consume_current_piece(Runtime::PlayerSessionState& player){
    if (player.pieces.bag.empty()) throw std::logic_error("cannot consume current piece from an empty queue");
    player.pieces.bag.pop_front();
}
void prepend_piece(Runtime::PlayerSessionState& player,Piece::Type pieceType,bool startsBag){ player.pieces.bag.push_front({pieceType,startsBag}); }

std::vector<Piece::Type> unseen_pieces_in_preview_endpoint_bag(
    const Runtime::GameContext& context, const Runtime::PlayerSessionState& player){
    // BOT bag accounting supports one copy of each piece only.
    // The 2026 Fool modes also vary bag composition dynamically, so there is no
    // fixed n-bag membership to expose to BOTs.
    if(Runtime::effective_bag_randomizer(context.config)!=Runtime::BagRandomizer::SevenBag ||
       finite_map_sequence(context) ||
       context.config.quickPlay.fool == Runtime::FoolMode::AprilFools2026 ||
       context.config.quickPlay.fool == Runtime::FoolMode::ReverseAprilFools2026){
        return {};
    }

    const auto& queue=player.pieces.bag;
    const int configuredNext=std::max(0,player.gameplay.playfield.nextCount);
    const std::size_t endpoint=static_cast<std::size_t>(configuredNext);
    if(endpoint>=queue.size()) return {};

    std::vector<Piece::Type> unseen;
    for(std::size_t i=endpoint+1;i<queue.size();++i){
        if(queue[i].startsNewBag) break;
        unseen.push_back(queue[i].type);
    }
    std::sort(unseen.begin(),unseen.end());
    unseen.erase(std::unique(unseen.begin(),unseen.end()),unseen.end());
    return unseen;
}

} // namespace Engine
