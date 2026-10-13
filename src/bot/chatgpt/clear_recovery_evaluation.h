#ifndef TETR_BOT_CHATGPT_CLEAR_RECOVERY_EVALUATION_H
#define TETR_BOT_CHATGPT_CLEAR_RECOVERY_EVALUATION_H

#include "evaluation.h"
#include "integer_utils.h"
#include "fast_terrain_evaluation.h"
#include <array>
#include <memory>

namespace Bot_chatgpt {
// Search-order experiment. A newly covered empty cell receives a hole-cost
// refund only after a legal, visible one/two-clear continuation removes or
// uncovers every such cell. Clears stall garbage. Unknown risen geometry is
// excluded. The hint ends after depth four and never changes rule state.
class ClearRecoveryEvaluation : public Evaluation {
    friend struct RecoveryWitnessAudit;
    using Mask = std::array<unsigned, 40>;
    using Key = std::array<unsigned, 86>;
    struct Entry { Key key{}; std::uint64_t generation=0; double refund=0; };
    static constexpr std::size_t tableSize=1024, queryLimit=256;
    mutable std::unique_ptr<std::array<Entry,tableSize>> table_=std::make_unique<std::array<Entry,tableSize>>();
    mutable std::array<Search,2> moves_;
    Search::Config config_;
    const m_tetris::TetrisContext* context_=nullptr;
    std::uint64_t generation_=1;
    bool allowSetup_=false;
public:
    struct Statistics {std::size_t queries=0,hits=0,proofs=0,refunds=0,limited=0,enumerations=0;};
private:
    mutable Statistics stats_;
    static Mask holes(const m_tetris::TetrisMap& map) {
        Mask result{};unsigned covered=0;
        for(int y=map.roof-1;y>=0;--y){covered|=map.row[y];result[y]=covered&~map.row[y];}
        return result;
    }
    static double hole_cost(const m_tetris::TetrisMap& map,const Mask& ignored={}) {
        const auto all=holes(map);int lines=0,depth=0,runs[10]{};double cover=0,rate=32;
        for(int y=map.roof-1;y>=0;--y){const unsigned h=all[y]&~ignored[y];
            if(h){++lines;int width=0;for(int high=y+1;high<map.roof;++high){const auto bits=h&map.row[high];if(!bits)break;width+=(high+1)*ZZZ_BitCount(bits);}cover+=rate*width;rate*=.25;}
            for(int x=0;x<10;++x){runs[x]=(h&(1u<<x))?runs[x]+1:0;depth+=runs[x];}
        }
        return 380.0*lines+40.0*depth+cover;
    }
    static Mask shift_marks(const Mask& marks,std::uint64_t cleared) {
        Mask result{};int out=0;for(int y=0;y<40;++y)if(!((cleared>>y)&1))result[out++]=marks[y];return result;
    }
    std::uint64_t clear_rows(const m_tetris::TetrisMap& before,Landing landing) const {
        std::uint64_t result=0;for(int y=0;y<landing->height;++y){int row=landing->row+y;if((before.row[row]|landing->data[y])==context_->full())result|=std::uint64_t{1}<<row;}return result;
    }
    bool recovered(const m_tetris::TetrisMap& map,const Mask& marks) const {
        const auto covered=holes(map);for(int y=0;y<40;++y)if(covered[y]&marks[y])return false;return true;
    }
    bool safe_spawn(const m_tetris::TetrisMap& map,const char* next,std::size_t length,bool cleared=true) const {
        if(length)return spawn_node(context_,map,*next,cleared)!=nullptr;
        for(char t:std::string("IJLOSTZ"))if(!spawn_node(context_,map,t,cleared))return false;
        return true;
    }
    bool prove(const m_tetris::TetrisMap& map,const Mask& marks,const char* next,std::size_t length,
               char held,bool previousClear,int remaining,bool allowNonClear=false,
               std::vector<Landing>* witness=nullptr) const {
        if(!length)return false;
        struct Setup {m_tetris::TetrisMap map;Mask marks;char hold;Landing landing;double value;};
        std::vector<Setup> setups;
        // Nonempty HOLD is certainly available in the native environment.
        // Empty-HOLD lookahead is omitted: blank also encodes disabled HOLD.
        for(int swap=0;swap<2;++swap){const char family=swap?held:*next;
            if(swap&&(held==' '||held==*next))continue;
            const auto*spawn=spawn_node(context_,map,family,previousClear);if(!spawn)continue;
            if(allowSetup_&&stats_.enumerations>=256)return false;
            ++stats_.enumerations;
            const auto*landings=moves_[remaining-1].search(map,spawn,1);
            for(auto landing:*landings){const auto cleared=clear_rows(map,landing);
                if(!cleared&&!(allowNonClear&&remaining>1))continue;
                auto after=map;landing->attach(context_,after);
                if(!safe_spawn(after,next+1,length-1,cleared!=0))continue;
                const auto shifted=shift_marks(marks,cleared);
                if(!cleared){
                    setups.push_back({after,shifted,swap?*next:held,landing,fast_native_terrain(after)});
                    std::stable_sort(setups.begin(),setups.end(),[](const auto&a,const auto&b){return a.value>b.value;});
                    if(setups.size()>4)setups.pop_back();
                    continue;
                }
                if(recovered(after,shifted)){if(witness)witness->push_back(landing);return true;}
                if(remaining>1&&prove(after,shifted,next+1,length-1,swap?*next:held,true,remaining-1,false,witness)){
                    if(witness)witness->insert(witness->begin(),landing);
                    return true;
                }
            }
        }
        for(const auto&s:setups)if(prove(s.map,s.marks,next+1,length-1,s.hold,false,remaining-1,false,witness)){
            if(witness)witness->insert(witness->begin(),s.landing);
            return true;
        }
        return false;
    }
protected:
    void enable_setup_recovery(){allowSetup_=true;}
public:
    void init(const m_tetris::TetrisContext* context) {
        Evaluation::init(context);context_=context;for(auto&search:moves_)search.init(context,&config_);
    }
    void set_garbage_forecast(const std::vector<VisiblePacket>& packets,double step) {
        Evaluation::set_garbage_forecast(packets,step);stats_={};
        if(++generation_==0){for(auto&e:*table_)e.generation=0;++generation_;}
    }
    const Statistics& statistics()const{return stats_;}
    Status get(Landing& landing,const Result& evaluation,std::size_t depth,
               const Status& previous,const m_tetris::TetrisContext::Env& env) const {
        auto result=Evaluation::get(landing,evaluation,depth,previous,env);
        if(previous.dead||result.dead||result.risen||depth>3||!env.length||!evaluation.before)return result;
        const auto&map=*evaluation.shape.map;
        Key key{};std::copy_n(map.row,40,key.begin());std::copy_n(evaluation.before->row,40,key.begin()+40);
        key[80]=evaluation.clearedRows;key[81]=evaluation.clearedRows>>32;
        key[82]=static_cast<unsigned char>(*env.next);key[83]=env.length>1?static_cast<unsigned char>(env.next[1]):0;
        const bool allowNonClear=allowSetup_&&result.incoming==0;
        key[84]=static_cast<unsigned char>(env.hold);key[85]=unsigned(result.previousCleared)|(allowNonClear?2u:0u);
        std::uint64_t hash=1469598103934665603ull;for(auto x:key)hash=(hash^x)*1099511628211ull;
        auto&entry=(*table_)[hash&(tableSize-1)];
        if(entry.generation==generation_&&entry.key==key){++stats_.hits;if(entry.refund>0){result.value+=entry.refund;++stats_.refunds;}return result;}
        if(stats_.queries>=queryLimit){++stats_.limited;return result;}
        const auto old=shift_marks(holes(*evaluation.before),evaluation.clearedRows);auto added=holes(map);bool any=false;
        for(int y=0;y<40;++y){added[y]&=~old[y];any|=added[y]!=0;}if(!any)return result;
        const double refund=std::max(0.0,hole_cost(map)-hole_cost(map,added));if(refund<=0)return result;
        ++stats_.queries;const bool proven=prove(map,added,env.next,std::min<std::size_t>(2,env.length),env.hold,result.previousCleared,2,allowNonClear);
        entry={key,generation_,proven?refund:0};stats_.proofs+=proven;
        if(proven){result.value+=refund;++stats_.refunds;}
        return result;
    }
};
class SetupRecoveryEvaluation : public ClearRecoveryEvaluation {
public:
    SetupRecoveryEvaluation(){enable_setup_recovery();}
};
}
#endif
