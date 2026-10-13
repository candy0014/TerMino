#ifndef TETR_BOT_CHATGPT_BAG_TAIL_ENGINE_H
#define TETR_BOT_CHATGPT_BAG_TAIL_ENGINE_H

#include "evaluation.h"
#include "rule_tetr_srs_plus.h"
#include <chrono>
#include <map>
#include <set>

namespace Bot_chatgpt {
// Experimental frontier valuation. The original tree searches visible pieces.
// At its six-piece frontier, compare a small pool of existing plans against
// every possible next family in the public bag complement. No hidden queue,
// future attack, or garbage hole is supplied. Each draw can use the known HOLD.
template<class Eval = Evaluation>
class BagTailEngine {
    using Native = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, Eval, Search>;
    using Node = typename Native::TreeNode;
    using Builder = typename Native::ContextBuilder;
    using Clock = std::chrono::steady_clock;
    std::deque<Node> nodes_;
    std::shared_ptr<m_tetris::TetrisContext> context_;
    typename Builder::template LocalContext<Node> local_{&nodes_};
    Node* root_ = nullptr;
    Eval ai_;
    Search search_, tailSearch_;
    typename Eval::Status status_;
    std::uint64_t memoryLimit_ = 64ull << 20;
    std::vector<int> unseen_;
    bool random_ = false;
    bool lastHold_ = false;
    std::size_t visibleCount_ = 0;
    void bind() {
        local_.engine = context_.get(); local_.ai = &ai_; local_.search = &search_;
        Builder::init_ai(ai_, &local_, context_.get());
        Builder::init_search(search_, &local_, context_.get());
        tailSearch_.init(context_.get(), local_.search_config());
    }
public:
    using Status = typename Eval::Status;
    struct RunResult { Landing target; Status status; bool change_hold = false; };
    struct Statistics {
        int pool = 0, evaluated = 0, draws = 0, landings = 0;
        bool changed = false, eligible = false, incomplete = false;
        double baselineExpected = 0, selectedExpected = 0, tailMs = 0;
    };
private:
    Statistics stats_;
    struct Candidate { Node* leaf; Node* first; char hold; };
    Node* best_leaf() const {
        for(std::size_t i=0;i<local_.wait.size()&&i<local_.sort.size();++i){
            Node*a=local_.wait[i].empty()?nullptr:local_.wait[i].top();
            Node*b=local_.sort[i].empty()?nullptr:local_.sort[i].top();
            if(a||b)return !a?b:!b?a:a->status.get().value>b->status.get().value?a:b;
        }
        return nullptr;
    }
    Node* first_node(Node*n) const {while(n&&n->parent&&n->parent!=root_)n=n->parent;return n;}
    RunResult result_for(Node*leaf) const {
        if(!leaf)return {};
        auto*first=first_node(leaf);
        return {first->identity,leaf->status.get_raw(),lastHold_&&bool(first->is_hold)};
    }
    bool candidate(Node*leaf,Candidate&out) const {
        if(!leaf||leaf==root_)return false;
        const auto&s=leaf->status.get_raw();
        if(s.dead||s.risen||s.placedPieces-status_.placedPieces!=int(visibleCount_))return false;
        // Walk the actual path, including HOLD, to certify visible consumption.
        std::vector<Node*>path;for(auto*n=leaf;n!=root_;n=n->parent){if(!n)return false;path.push_back(n);}
        std::reverse(path.begin(),path.end());int cursor=0;char held=root_->hold;
        for(auto*n:path){
            if(cursor>=int(local_.next.size()))return false;
            const char current=local_.next[cursor];
            if(n->is_hold){
                if(held==' ')return false; // Empty-HOLD roots use native selection.
                if(n->identity->status.t!=held)return false;
                held=current;
            }else if(n->identity->status.t!=current)return false;
            ++cursor;
        }
        if(cursor!=int(local_.next.size()))return false;
        out={leaf,path.front(),lastHold_?held:' '};return true;
    }
    std::vector<int> possible_draws() const {
        if(!random_&&!unseen_.empty())return unseen_;
        return {0,1,2,3,4,5,6};
    }
    bool expected_value(const Candidate&c,const std::vector<int>&draws,
                        Clock::time_point deadline,double&out){
        constexpr char types[]="IJLOSTZ";
        const auto&previous=c.leaf->status.get_raw();double total=0;
        for(int family:draws){
            if(Clock::now()>=deadline)return false;
            ++stats_.draws;
            const char current=types[family];
            std::vector<int>following=draws;
            if(!random_){following.erase(std::remove(following.begin(),following.end(),family),following.end());if(following.empty())following={0,1,2,3,4,5,6};}
            const auto*active=spawn_node(context_.get(),c.leaf->map,current,previous.previousCleared);
            if(!active){total+=previous.value-1e9;continue;}
            double best=-1e100;
            for(int swap=0;swap<2;++swap){
                if(swap&&(!lastHold_||c.hold==' '||c.hold==current))continue;
                const char piece=swap?c.hold:current;
                const auto*spawn=swap?spawn_node(context_.get(),c.leaf->map,piece,previous.previousCleared):active;
                if(!spawn)continue;
                for(auto landing:*tailSearch_.search(c.leaf->map,spawn,1)){
                    ++stats_.landings;auto map=c.leaf->map;
                    const int lines=landing->attach(context_.get(),map);
                    const auto ev=ai_.eval(landing,map,c.leaf->map,lines);
                    const char held=swap?current:c.hold;
                    const m_tetris::TetrisContext::Env env{nullptr,0,piece,held,bool(swap)};
                    const auto state=ai_.get(landing,ev,visibleCount_,previous,env);
                    double score=state.value;
                    // The next spawn is unknown. Average its failure indicator
                    // over the remaining bag, rather than assuming it fits.
                    if(!state.dead){int blocked=0;for(int t:following)blocked+=!spawn_node(context_.get(),map,types[t],lines>0,state.risen);score-=1e9*blocked/following.size();}
                    best=std::max(best,score);
                }
            }
            total+=best==-1e100?previous.value-1e9:best;
        }
        out=total/draws.size();return true;
    }
    RunResult refine(Clock::time_point deadline){
        stats_={};auto start=Clock::now();Node*native=best_leaf();auto original=result_for(native);
        const auto finish=[&](RunResult r){stats_.tailMs=std::chrono::duration<double,std::milli>(Clock::now()-start).count();return r;};
        if(!native||visibleCount_!=6||(lastHold_&&root_->hold==' ')||original.status.risen||original.status.dead)return finish(original);
        Node*prefix=native;while(prefix!=root_&&prefix->status.get_raw().placedPieces-status_.placedPieces>int(visibleCount_))prefix=prefix->parent;
        Candidate base;if(!candidate(prefix,base))return finish(original);
        stats_.eligible=true;
        std::vector<Candidate>pool{base};
        // Remaining-depth heap 1 is the known six-piece frontier when the
        // native search has a seventh known-HOLD estimate. No tree mutation.
        const int level=int(local_.max_length)+1-int(visibleCount_);
        std::set<Node*>seen{prefix};std::map<Node*,int>perFirst{{base.first,1}};
        if(level>=0&&level<int(local_.wait.size())){
            auto waiting=local_.wait[level],expanded=local_.sort[level];
            for(int scanned=0;scanned<256&&pool.size()<16&&(!waiting.empty()||!expanded.empty());++scanned){
                Node*n=nullptr;
                if(expanded.empty()||(!waiting.empty()&&waiting.top()->status.get().value>=expanded.top()->status.get().value)){n=waiting.top();waiting.pop();}
                else{n=expanded.top();expanded.pop();}
                if(!seen.insert(n).second)continue;
                Candidate c;
                if(!candidate(n,c)||perFirst[c.first]>=2)continue;
                ++perFirst[c.first];pool.push_back(c);
            }
        }
        stats_.pool=pool.size();const auto draws=possible_draws();double best=0;Node*chosen=prefix;
        for(std::size_t i=0;i<pool.size();++i){
            double score=0;if(!expected_value(pool[i],draws,deadline,score)){stats_.incomplete=true;break;}
            ++stats_.evaluated;if(i==0){best=score;stats_.baselineExpected=score;}
            else if(score>best){best=score;chosen=pool[i].leaf;}
        }
        if(!stats_.evaluated)return finish(original);
        stats_.selectedExpected=best;
        auto result=result_for(chosen);stats_.changed=!(result.target==original.target)||result.change_hold!=original.change_hold;
        // Attack/B2B fields describe the certified six known placements.
        // Only value holds an expectation; hypothetical attack is not reported.
        result.status.value=best;return finish(result);
    }
    template<bool Hold>RunResult run_impl(const m_tetris::TetrisMap&map,const m_tetris::TetrisNode*node,
        char held,bool holdFree,const char*next,std::size_t length,int ms){
        if(!context_||!node||!node->check(map))return {};
        const auto started=Clock::now();const auto deadline=started+std::chrono::milliseconds(std::max(0,ms));
        lastHold_=Hold;visibleCount_=length+1;
        if constexpr(Hold)root_=root_->update(&local_,map,status_,node,held,!holdFree,next,length);
        else root_=root_->update(&local_,map,status_,node,next,length);
        const auto nativeDeadline=deadline-std::chrono::milliseconds(ms>10?7:0);
        do{if(root_->template run<Hold>(&local_))break;}while(Clock::now()<nativeDeadline);
        if constexpr(Hold)if(root_->hold==' '&&local_.next.size()==1&&!root_->is_hold_lock)return {{},{},true};
        return ms>10?refine(deadline):result_for(best_leaf());
    }
public:
    explicit BagTailEngine(std::shared_ptr<m_tetris::TetrisContext>context={}):context_(std::move(context)){
        nodes_.emplace_back();root_=&nodes_.back();if(context_)bind();
    }
    bool prepare(int w,int h){if(context_&&context_->width()==w&&context_->height()==h)return true;Native geometry;if(!geometry.prepare(w,h))return false;context_=geometry.context();bind();return true;}
    const auto&context()const{return context_;}Status*status(){return &status_;}Eval*ai(){return &ai_;}auto*search_config(){return local_.search_config();}
    void memory_limit(std::uint64_t n){memoryLimit_=std::clamp<std::uint64_t>(n,32ull<<20,2ull<<30);}auto memory_limit()const{return memoryLimit_;}
    std::uint64_t memory_usage()const{return context_->node_max()*sizeof(m_tetris::TetrisNode)+(nodes_.size()-local_.free_count)*sizeof(Node);}
    void update(){local_.is_complete=false;root_->update_version(&local_);}
    void set_bag_state(const std::vector<int>&unseen,bool random){unseen_=unseen;std::sort(unseen_.begin(),unseen_.end());unseen_.erase(std::unique(unseen_.begin(),unseen_.end()),unseen_.end());for(int t:unseen_)if(t<0||t>6)throw std::runtime_error("invalid bag family");random_=random;}
    const Statistics&statistics()const{return stats_;}
    RunResult run(const m_tetris::TetrisMap&m,const m_tetris::TetrisNode*n,const char*p,std::size_t len,int ms=100){return run_impl<false>(m,n,' ',false,p,len,ms);}
    RunResult run_hold(const m_tetris::TetrisMap&m,const m_tetris::TetrisNode*n,char h,bool free,const char*p,std::size_t len,int ms=100){return run_impl<true>(m,n,h,free,p,len,ms);}
};
}
#endif
