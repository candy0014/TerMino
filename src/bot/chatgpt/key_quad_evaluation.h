#include "tetr/util/bit_ops.h"
#ifndef TETR_BOT_CHATGPT_KEY_QUAD_EVALUATION_H
#define TETR_BOT_CHATGPT_KEY_QUAD_EVALUATION_H

#include "evaluation.h"
#include <array>
#include <optional>
#include <set>
#include <string_view>

namespace Bot_chatgpt {
// First bounded implementation of key-piece projection. Recognize a concrete
// I quad, check its entry path, and evaluate the field after that virtual clear.
// Earlier nodes can also recognize one legal preparation which creates it.
// Only value changes; physical attack, chain, board and garbage remain actual.
class KeyQuadEvaluation : public Evaluation {
    struct Supply { std::string_view next; char hold=' '; unsigned unseen=127; bool bag=false; };
    struct Arrival { int eta=99; bool hold=false; char heldAfter=' '; };
    struct Quad { Landing move; m_tetris::TetrisMap after{10,40}; };
    const m_tetris::TetrisContext* context_=nullptr;
    mutable Search preparationSearch_;
    Search::Config config_;
    std::array<std::array<const m_tetris::TetrisNode*,4>,7> anchors_{};
    unsigned unseen_=127;
    std::size_t visibleCount_=6;
    bool standardBag_=true, supportedHold_=true;
public:
    struct Statistics { std::uint64_t calls=0, ready=0, weak=0, geometries=0, enumerations=0; };
private:
    mutable Statistics stats_;
    static constexpr int maxWait=4;
    static bool single(unsigned mask){return mask&&!(mask&(mask-1));}
    static Arrival arrival(Supply supply){
        Arrival result;
        auto at=supply.next.find('I');
        if(at!=std::string_view::npos)result={int(at)+1,false,supply.hold};
        if(supply.hold=='I'&&result.eta>1){
            result={1,true,supply.next.empty()?' ':supply.next.front()};
        }
        if(supply.bag&&(supply.unseen&1u)){
            const int bound=int(supply.next.size())+Util::popcount32(supply.unseen);
            if(bound<result.eta)result={bound,false,supply.hold};
        }
        return result;
    }
    std::optional<Quad> quad(const m_tetris::TetrisMap&map,bool previousClear)const{
        if(map.roof<4)return {};
        for(int y=0;y+3<map.roof;++y){
            const unsigned hole=1023u^map.row[y];
            if(!single(hole)||map.row[y+1]!=map.row[y]||map.row[y+2]!=map.row[y]||map.row[y+3]!=map.row[y])continue;
            const int x=Util::trailing_zeros32(hole);
            if(map.top[x]!=y)continue; // The entire entry column must be open.
            auto*spawn=spawn_node(context_,map,'I',previousClear);
            if(!spawn)continue;
            for(auto kicks:{spawn->wall_kick_clockwise,spawn->wall_kick_counterclockwise}){
                const m_tetris::TetrisNode* node=nullptr;
                for(int k=0;k<m_tetris::max_wall_kick&&kicks[k];++k)if(kicks[k]->check(map)){node=kicks[k];break;}
                if(!node)continue;
                while(node->col<x&&node->move_right&&node->move_right->check(map))node=node->move_right;
                while(node->col>x&&node->move_left&&node->move_left->check(map))node=node->move_left;
                if(node->col!=x)continue;
                node=node->drop(map);
                if(!node||node->row!=y||node->height!=4)continue;
                Quad out;out.move=Landing(node);out.after=map;
                if(node->attach(context_,out.after)==4)return out;
            }
        }
        return {};
    }
    double projected(const Quad&key,const m_tetris::TetrisMap&before,const Status&physical,
                     Supply supply,int depth,int extraDelay=0)const{
        const auto when=arrival(supply);
        if(when.eta+extraDelay>maxWait)return -1e100;
        auto previous=physical;
        if(when.eta>1)previous.combo=0; // Waiting non-clears do not retain combo.
        auto move=key.move;
        auto evaluated=Evaluation::eval(move,key.after,before,4);
        const m_tetris::TetrisContext::Env env{nullptr,0,'I',when.heldAfter,when.hold};
        auto future=Evaluation::get(move,evaluated,depth+when.eta,previous,env);
        if(future.dead||future.risen)return -1e100;
        return future.value;
    }
    double weak_value(const m_tetris::TetrisMap&map,const Status&physical,Supply supply,int depth)const{
        if(depth>2||arrival(supply).eta>maxWait)return -1e100;
        char current=' ';
        Supply following=supply;
        if(!supply.next.empty()){current=supply.next.front();following.next.remove_prefix(1);}
        else if(supply.bag&&single(supply.unseen)){
            current="IJLOSTZ"[Util::trailing_zeros32(supply.unseen)];following.unseen=127;
        }else return -1e100;
        if(!spawn_node(context_,map,current,physical.previousCleared))return -1e100;
        // Only rows bordering an existing three-row I well can participate in
        // this first repair detector. No arbitrary full-move beam is launched.
        using Mask=std::array<unsigned,40>;
        std::vector<Mask> requirements;
        for(int y=0;y+2<map.roof;++y){
            const unsigned h=1023u^map.row[y];
            if(!single(h)||map.row[y+1]!=map.row[y]||map.row[y+2]!=map.row[y])continue;
            Mask needed{};int cells=0;
            // Every occupied cell above the well in the I entry column must
            // disappear. A single preparation must finish ALL such rows.
            for(int r=y+3;r<map.roof;++r)if(map.row[r]&h){
                needed[r]=1023u^map.row[r];cells+=Util::popcount32(needed[r]);
            }
            if(cells>4)continue;
            int fourth=y+3;
            while(fourth<map.roof&&(map.row[fourth]&h))++fourth;
            if(fourth>=40)continue;
            needed[fourth]=(1023u^map.row[fourth])&~h;
            cells+=Util::popcount32(needed[fourth]);
            if(cells>0&&cells<=4&&std::find(requirements.begin(),requirements.end(),needed)==requirements.end())requirements.push_back(needed);
        }
        if(requirements.empty())return -1e100;
        double best=-1e100;
        for(int swap=0;swap<2;++swap){
            const char family=swap?supply.hold:current;
            if(swap&&(family==' '||family==current))continue;
            auto next=following;next.hold=swap?current:supply.hold;
            if(arrival(next).eta+1>maxWait)continue;
            auto*spawn=spawn_node(context_,map,family,physical.previousCleared);if(!spawn)continue;
            std::set<std::uint32_t> seen;
            std::optional<std::vector<Landing>> legal;
            const int index=piece_index(family);
            for(const auto&required:requirements){
                int row=0;while(row<40&&!required[row])++row;
                const int x=Util::trailing_zeros32(required[row]);
                for(auto*anchor:anchors_[index])if(anchor){
                    for(int ay=0;ay<anchor->height;++ay)for(unsigned bits=anchor->data[ay];bits;bits&=bits-1){
                        const int ax=Util::trailing_zeros32(bits);
                        const int nx=int(anchor->status.x)+x-ax,ny=int(anchor->status.y)+row-anchor->row-ay;
                        if(nx< -4||nx>10||ny<0||ny>=44)continue;
                        auto*node=context_->get(family,nx,ny,anchor->status.r);
                        if(!node||node->row<0||node->row+node->height>40)continue;
                        bool covers=true;
                        for(int r=0;r<40&&covers;++r)if(required[r])covers=r>=node->row&&r<node->row+node->height&&
                            (node->data[r-node->row]&required[r])==required[r];
                        if(!covers||!seen.insert(node->index_filtered).second)continue;
                        ++stats_.geometries;
                        if(!node->check(map)||(node->move_down&&node->move_down->check(map)))continue;
                        auto after=map;int lines=node->attach(context_,after);
                        if(after.roof>16)continue;
                        auto key=quad(after,lines>0);if(!key)continue;
                        // Full movement enumeration only after geometry proves
                        // that this particular preparation creates an I quad.
                        if(!legal){++stats_.enumerations;legal=*preparationSearch_.search(map,spawn,1);}
                        auto found=std::find_if(legal->begin(),legal->end(),[&](const auto&l){return l->index_filtered==node->index_filtered;});
                        if(found==legal->end())continue;
                        auto move=*found;
                        auto ev=Evaluation::eval(move,after,map,lines);
                        const m_tetris::TetrisContext::Env env{next.next.data(),next.next.size(),family,next.hold,bool(swap)};
                        auto prepared=Evaluation::get(move,ev,depth+1,physical,env);
                        if(prepared.dead||prepared.risen||prepared.incoming)continue;
                        // projected() expects the depth of the already placed
                        // piece; preparation consumes one actual future slot.
                        const double value=projected(*key,after,prepared,next,depth+1,1);
                        best=std::max(best,physical.value+.5*std::max(0.0,value-physical.value));
                    }
                }
            }
        }
        return best;
    }
public:
    void init(const m_tetris::TetrisContext*context){
        Evaluation::init(context);context_=context;preparationSearch_.init(context,&config_);
        for(int t=0;t<7;++t)for(int r=0;r<4;++r)anchors_[t][r]=context->get("IJLOSTZ"[t],3,22,r);
    }
    void set_key_state(std::size_t visible,int rootHold,const std::vector<int>&unseen,bool random){
        visibleCount_=visible;supportedHold_=rootHold!=-1;standardBag_=!random;unseen_=0;
        for(int t:unseen)if(t>=0&&t<7)unseen_|=1u<<t;
        if(!unseen_)unseen_=127;
    }
    void set_garbage_forecast(const std::vector<VisiblePacket>&packets,double step){
        Evaluation::set_garbage_forecast(packets,step);stats_={};
    }
    const Statistics&statistics()const{return stats_;}
    Status get(Landing&landing,const Result&evaluated,std::size_t depth,const Status&previous,
               const m_tetris::TetrisContext::Env&env)const{
        auto result=Evaluation::get(landing,evaluated,depth,previous,env);++stats_.calls;
        const auto&map=*evaluated.shape.map;
        if(previous.dead||result.dead||result.risen||result.incoming||!supportedHold_||depth>=visibleCount_||map.roof>16)return result;
        Supply supply{std::string_view(env.next?env.next:"",env.length),env.hold,unseen_,standardBag_};
        if(arrival(supply).eta>maxWait)return result;
        auto key=quad(map,result.previousCleared);
        if(key){const double value=projected(*key,map,result,supply,depth);if(value>result.value){result.value=value;++stats_.ready;}}
        else {const double value=weak_value(map,result,supply,depth);if(value>result.value){result.value=value;++stats_.weak;}}
        return result;
    }
};
}
#endif
