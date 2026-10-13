#ifndef TETR_BOT_CHATGPT_GARBAGE_ROLLOUT_ENGINE_H
#define TETR_BOT_CHATGPT_GARBAGE_ROLLOUT_ENGINE_H

#include "evaluation.h"
#include "rule_tetr_srs_plus.h"
#include <chrono>
#include <limits>
#include <optional>

namespace Bot_chatgpt {

// Store only the already visible immutable forecast supplied by getpos.
class RolloutSettings : public Evaluation {
public:
    PolicyWeights policy;
    std::vector<VisiblePacket> packets;
    double step=0;
    void set_policy(const PolicyWeights&p){policy=p;Evaluation::set_policy(p);}
    void set_garbage_forecast(const std::vector<VisiblePacket>&p,double s){packets=p;step=s;Evaluation::set_garbage_forecast(p,s);}
};

// Experimental determinization: ten hypothetical straight-hole packet layouts,
// never actual future garbage. Same root move is scored in every scenario.
// Later greedy decisions can be optimistic before another packet is revealed;
// this is a bounded decision heuristic, not an exact chance-tree solution.
class GarbageRolloutEngine {
    friend struct GarbageRolloutTestAccess;
    using Native=m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule,Evaluation,Search>;
    using Clock=std::chrono::steady_clock;
    Native native_;
    RolloutSettings settings_;
    Evaluation evaluator_;
    ai_zzz::Botris terrain_;
    Search search_;
    Search::Config config_;
    struct State {
        m_tetris::TetrisMap map{10,40};
        Evaluation::Status status;
        std::size_t cursor=0;
        char held=' ';
    };
    struct Choice {Landing landing;std::size_t cursor;char held;bool swap;};
    struct Ranked {Choice choice;double value;};
    std::string visible_;
    bool canHold_=true,rootHoldFree_=true;
    int totalIncoming_=0;
    int continuationWidth_=1;
    std::vector<int> packetEnds_;

    void bind(){settings_.init(context().get());evaluator_.init(context().get());terrain_.init(context().get(),nullptr);search_.init(context().get(),&config_);}
    static void write_row(m_tetris::TetrisMap&map,int y,unsigned bits) {
        map.row[y]=bits;
        for(int x=0;x<10;++x)if(bits&(1u<<x)){map.top[x]=y+1;map.roof=y+1;++map.count;}
    }
    int hole(int consumed,int scenario)const {
        const auto packet=std::upper_bound(packetEnds_.begin(),packetEnds_.end(),consumed)-packetEnds_.begin();
        return (scenario+3*int(packet))%10;
    }
    void insert_garbage(State&state,int rise,int consumed,int scenario)const {
        m_tetris::TetrisMap shifted(10,40);
        for(int y=0;y<rise;++y)write_row(shifted,y,1023u^(1u<<hole(consumed+rise-1-y,scenario)));
        for(int y=0;y<40-rise;++y)write_row(shifted,y+rise,state.map.row[y]);
        if(state.map.roof+rise>40) {
            if(!state.status.dead)state.status.value-=1e9;
            state.status.dead=true;
        }
        state.map=shifted;state.status.garbageHeight+=rise;state.status.risen=0;
    }
    State advance(const State&parent,Choice choice,int depth,int scenario)const {
        State child=parent;
        const int lines=choice.landing->attach(context().get(),child.map);
        const auto evaluated=evaluator_.eval(choice.landing,child.map,parent.map,lines);
        const char*next=choice.cursor<visible_.size()?visible_.data()+choice.cursor:nullptr;
        const m_tetris::TetrisContext::Env env{next,visible_.size()-choice.cursor,choice.landing->status.t,choice.held,choice.swap};
        child.status=evaluator_.get(choice.landing,evaluated,depth,parent.status,env);
        child.cursor=choice.cursor;child.held=choice.held;
        const int rise=child.status.risen;
        if(rise>0) {
            // A non-clear has zero attack, so consumed before this rise is
            // exactly total visible FIFO minus the parent's remaining count.
            insert_garbage(child,rise,totalIncoming_-parent.status.incoming,scenario);
            const double shape=child.map.roof?terrain_.eval(ai_zzz::Botris::TetrisNodeEx(choice.landing.node),child.map,parent.map,0).value:-16000.0;
            child.status.value+=shape-evaluated.shape.value;
            if(next&&!spawn_node(context().get(),child.map,*next,lines>0)) {
                if(!child.status.dead)child.status.value-=1e9;
                child.status.dead=true;
            }
        }
        return child;
    }
    template<class Callback> void choices(const State&s,const m_tetris::TetrisNode*actual,int depth,Callback callback) {
        if(s.cursor>=visible_.size())return;
        const char active=visible_[s.cursor];
        auto enumerate=[&](char piece,std::size_t cursor,char held,bool swap) {
            const auto*start=actual&&!swap?actual:spawn_node(context().get(),s.map,piece,s.status.previousCleared);
            if(!start)return;
            for(const auto&landing:*search_.search(s.map,start,depth))callback(Choice{landing,cursor,held,swap});
        };
        enumerate(active,s.cursor+1,s.held,false);
        if(!canHold_||(depth==0&&!rootHoldFree_))return;
        if(s.held!=' '&&s.held!=active)enumerate(s.held,s.cursor+1,active,true);
        else if(s.held==' '&&s.cursor+1<visible_.size()&&visible_[s.cursor+1]!=active)
            enumerate(visible_[s.cursor+1],s.cursor+2,active,true);
    }
    std::optional<double> rollout(const State&initial,const Choice&first,int scenario,Clock::time_point deadline) {
        auto state=advance(initial,first,0,scenario);++stats_.evaluations;
        if(continuationWidth_>1) {
            std::vector<State> beam{state};
            for(int depth=1;depth<6;++depth) {
                if(Clock::now()>=deadline)return {};
                std::vector<State> children;
                bool continued=false;
                for(const auto&parent:beam) {
                    if(parent.status.dead||parent.cursor>=visible_.size()) {children.push_back(parent);continue;}
                    continued=true;
                    const auto before=children.size();
                    choices(parent,nullptr,depth,[&](Choice choice) {
                        children.push_back(advance(parent,choice,depth,scenario));++stats_.evaluations;
                    });
                    if(children.size()==before) {
                        auto dead=parent;dead.status.value-=1e9;dead.status.dead=true;children.push_back(dead);
                    }
                }
                std::stable_sort(children.begin(),children.end(),[](const State&a,const State&b){return a.status.value>b.status.value;});
                if(children.size()>std::size_t(continuationWidth_))children.resize(continuationWidth_);
                beam=std::move(children);
                if(!continued)break;
            }
            if(Clock::now()>=deadline)return {};
            return beam.front().status.value;
        }
        for(int depth=1;depth<6&&!state.status.dead&&state.cursor<visible_.size();++depth) {
            if(Clock::now()>=deadline)return {};
            std::optional<State> best;
            choices(state,nullptr,depth,[&](Choice choice) {
                auto child=advance(state,choice,depth,scenario);++stats_.evaluations;
                if(!best||child.status.value>best->status.value)best=std::move(child);
            });
            if(!best){state.status.value-=1e9;state.status.dead=true;break;}
            state=std::move(*best);
        }
        if(Clock::now()>=deadline)return {};
        return state.status.value;
    }
    template<bool Hold> typename Native::RunResult run_impl(const m_tetris::TetrisMap&map,
        const m_tetris::TetrisNode*start,char held,bool holdFree,const char*next,std::size_t length,int ms) {
        stats_={};
        native_.ai()->set_policy(settings_.policy);native_.ai()->set_garbage_forecast(settings_.packets,settings_.step);
        const auto begin=Clock::now();const auto deadline=begin+std::chrono::milliseconds(std::max(0,ms));
        const bool use=ms>0&&native_.status()->incoming>0&&start&&start->check(map);
        const int normalBudget=use?ms/2:ms;
        auto answer=Hold?native_.run_hold(map,start,held,holdFree,next,length,normalBudget):native_.run(map,start,next,length,normalBudget);
        if(!use||!answer.target.node)return answer;
        visible_.assign(1,start->status.t);
        if(length)visible_.append(next,length);
        canHold_=Hold;rootHoldFree_=holdFree;
        config_.rootPreviousCleared=native_.status()->previousCleared;
        evaluator_.set_policy(settings_.policy);evaluator_.set_garbage_forecast(settings_.packets,settings_.step);
        packetEnds_.clear();totalIncoming_=0;
        for(const auto&p:settings_.packets)if(p.lines>0){totalIncoming_+=p.lines;packetEnds_.push_back(totalIncoming_);}
        if(totalIncoming_!=native_.status()->incoming)return answer;
        State initial;initial.map=map;initial.status=*native_.status();initial.held=Hold?held:' ';
        std::vector<Ranked> options;
        choices(initial,start,0,[&](Choice choice){
            auto after=map;const int lines=choice.landing->attach(context().get(),after);
            auto eval=evaluator_.eval(choice.landing,after,map,lines);
            const auto*tail=choice.cursor<visible_.size()?visible_.data()+choice.cursor:nullptr;
            const m_tetris::TetrisContext::Env env{tail,visible_.size()-choice.cursor,choice.landing->status.t,choice.held,choice.swap};
            auto move=choice.landing;const auto s=evaluator_.get(move,eval,0,initial.status,env);
            options.push_back({choice,s.value});
        });
        auto isBase=[&](const Ranked&r){return r.choice.landing==answer.target&&r.choice.swap==answer.change_hold;};
        const auto base=std::find_if(options.begin(),options.end(),isBase);
        if(base==options.end())return answer;
        std::iter_swap(options.begin(),base);
        std::stable_sort(options.begin()+1,options.end(),[](const Ranked&a,const Ranked&b){return a.value>b.value;});
        if(options.size()>8)options.resize(8);
        double best=-std::numeric_limits<double>::infinity();std::optional<Choice>winner;
        for(const auto&ranked:options) {
            double sum=0;bool complete=true;
            for(int scenario=0;scenario<10;++scenario) {
                if(Clock::now()>=deadline){complete=false;break;}
                auto score=rollout(initial,ranked.choice,scenario,deadline);
                if(!score){complete=false;break;}sum+=*score;++stats_.completeScenarios;
            }
            if(!complete)break;
            ++stats_.completeActions;
            const double mean=sum/10;
            if(mean>best){best=mean;winner=ranked.choice;}
        }
        if(winner) {
            stats_.changed=!(winner->landing==answer.target)||winner->swap!=answer.change_hold;
            stats_.meanValue=best;
            answer.target=winner->landing;answer.change_hold=winner->swap;
            // Diagnostics report the exact first transition, not the integer
            // attack/depth of a favorable hypothetical future scenario.
            auto after=map;const int lines=answer.target->attach(context().get(),after);
            const auto eval=evaluator_.eval(answer.target,after,map,lines);
            const auto*tail=winner->cursor<visible_.size()?visible_.data()+winner->cursor:nullptr;
            const m_tetris::TetrisContext::Env env{tail,visible_.size()-winner->cursor,answer.target->status.t,winner->held,winner->swap};
            answer.status=evaluator_.get(answer.target,eval,0,initial.status,env);
        }
        stats_.milliseconds=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
        return answer;
    }
public:
    using Status=Evaluation::Status;using RunResult=Native::RunResult;
    struct Statistics {std::size_t completeActions=0,completeScenarios=0,evaluations=0;bool changed=false;double meanValue=0,milliseconds=0;};
private:
    Statistics stats_;
public:
    explicit GarbageRolloutEngine(int continuationWidth=1):continuationWidth_(continuationWidth) {}
    explicit GarbageRolloutEngine(std::shared_ptr<m_tetris::TetrisContext>context,int continuationWidth=1):native_(std::move(context)),continuationWidth_(continuationWidth) {bind();}
    bool prepare(int width,int height){if(!native_.prepare(width,height))return false;bind();return true;}
    const std::shared_ptr<m_tetris::TetrisContext>&context()const{return native_.context();}
    Status*status(){return native_.status();}
    RolloutSettings*ai(){return &settings_;}
    auto*search_config(){return native_.search_config();}
    void memory_limit(std::uint64_t n){native_.memory_limit(n);}
    auto memory_limit()const{return native_.memory_limit();}
    auto memory_usage()const{return native_.memory_usage();}
    const auto&statistics()const{return stats_;}
    void update(){native_.update();}
    RunResult run(const m_tetris::TetrisMap&map,const m_tetris::TetrisNode*start,const char*next,std::size_t length,int ms=100){return run_impl<false>(map,start,' ',false,next,length,ms);}
    RunResult run_hold(const m_tetris::TetrisMap&map,const m_tetris::TetrisNode*start,char held,bool free,const char*next,std::size_t length,int ms=100){return run_impl<true>(map,start,held,free,next,length,ms);}
};
class GarbageBeamEngine : public GarbageRolloutEngine {
public:
    GarbageBeamEngine():GarbageRolloutEngine(2) {}
    explicit GarbageBeamEngine(std::shared_ptr<m_tetris::TetrisContext>context):GarbageRolloutEngine(std::move(context),2) {}
};
} // namespace Bot_chatgpt
#endif
