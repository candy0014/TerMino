#ifndef TETR_BOT_CHATGPT_ROOT_COVERAGE_ENGINE_H
#define TETR_BOT_CHATGPT_ROOT_COVERAGE_ENGINE_H

#include "coverage_search.h"
#include "evaluation.h"
#include "rule_tetr_srs_plus.h"
#include <chrono>
#include <limits>
#include <memory>

namespace Bot_chatgpt {
class RootCoverageSearch {
    Search fast_;
    CoverageSearch full_;
public:
    using Config=Search::Config;
    bool enabled=true;
    std::vector<Landing> rootLandings;
    void init(const m_tetris::TetrisContext*c,const Config*config){fast_.init(c,config);full_.init(c,config);}
    const std::vector<Landing>*search(const m_tetris::TetrisMap&map,const m_tetris::TetrisNode*start,std::size_t depth) {
        if(depth||!enabled)return fast_.search(map,start,depth);
        const auto*found=full_.search(map,start,depth);
        rootLandings.insert(rootLandings.end(),found->begin(),found->end());
        return found;
    }
};

// Reuse the native public tree and its unchanged widening schedule. Only the
// actual root gets full movement enumeration. A promoted node may retain a
// partial future landing list, so a version change explicitly merges the full
// root list and refreshes spin metadata before rescoring its children.
class RootCoverageEngine {
    friend struct RootCoverageTestAccess;
    using Native=m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule,Evaluation,RootCoverageSearch>;
    using Node=Native::TreeNode;
    using Builder=Native::ContextBuilder;
    using Clock=std::chrono::steady_clock;
    std::deque<Node> nodes_;
    std::shared_ptr<m_tetris::TetrisContext> context_;
    Builder::LocalContext<Node> local_{&nodes_};
    Node*root_=nullptr;
    Evaluation ai_;
    RootCoverageSearch search_;
    Evaluation::Status status_;
    std::uint64_t memoryLimit_=64ull<<20;
    std::size_t coverageVersion_=std::numeric_limits<std::size_t>::max();

    void bind() {
        local_.engine=context_.get();local_.ai=&ai_;local_.search=&search_;
        Builder::init_ai(ai_,&local_,context_.get());Builder::init_search(search_,&local_,context_.get());
    }
    template<bool Hold> void prepare_root() {
        if(!search_.enabled)return;
        if(coverageVersion_==local_.version)return;
        coverageVersion_=local_.version;
        search_.rootLandings.clear();
        // Nonempty sentinel forces native merge, including previously cached
        // empty geometry. Clearing the flag would append duplicate children.
        if(root_->children||!root_->node_flag.empty())root_->node_flag.set(&local_.virtual_flag);
        root_->is_dead=false;
        root_->template search_children<Hold>(&local_);
        for(auto*child=root_->children;child;child=child->children_next) {
            auto found=std::find_if(search_.rootLandings.begin(),search_.rootLandings.end(),[&](const Landing&landing){
                return landing.node==child->identity.node;
            });
            if(found!=search_.rootLandings.end()) {
                // Same map permits subtree reuse; eligibility/mini/kick can
                // improve even when the landing geometry already existed.
                auto landing=*found;
                Native::Core::eval(&local_,root_->map,landing,child);
            }
        }
    }
    template<bool Hold> typename Native::RunResult run_impl(const m_tetris::TetrisMap&map,
        const m_tetris::TetrisNode*node,char held,bool holdFree,const char*next,std::size_t length,int ms) {
        if(!context_||!node||!node->check(map))return {};
        const auto deadline=Clock::now()+std::chrono::milliseconds(std::max(0,ms));
        if constexpr(Hold)root_=root_->update(&local_,map,status_,node,held,!holdFree,next,length);
        else root_=root_->update(&local_,map,status_,node,next,length);
        prepare_root<Hold>();
        do {if(root_->template run<Hold>(&local_))break;}while(Clock::now()<deadline);
        if constexpr(Hold)if(root_->hold==' '&&local_.next.size()==1&&!root_->is_hold_lock)return typename Native::RunResult(true);
        const auto best=root_->get_best(&local_);
        return best.first?typename Native::RunResult(best,Hold&&best.first->is_hold):typename Native::RunResult(false);
    }
public:
    using Status=Evaluation::Status;using RunResult=Native::RunResult;
    explicit RootCoverageEngine(std::shared_ptr<m_tetris::TetrisContext>context={},bool enabled=true):context_(std::move(context)) {
        search_.enabled=enabled;nodes_.emplace_back();root_=&nodes_.back();if(context_)bind();
    }
    RootCoverageEngine(const RootCoverageEngine&)=delete;
    RootCoverageEngine&operator=(const RootCoverageEngine&)=delete;
    bool prepare(int width,int height) {
        if(context_&&context_->width()==width&&context_->height()==height)return true;
        Native geometry;if(!geometry.prepare(width,height))return false;
        context_=geometry.context();bind();return true;
    }
    const auto&context()const{return context_;}
    Status*status(){return &status_;}
    Evaluation*ai(){return &ai_;}
    auto*search_config(){return local_.search_config();}
    void memory_limit(std::uint64_t n){memoryLimit_=std::clamp<std::uint64_t>(n,32ull<<20,2ull<<30);}
    auto memory_limit()const{return memoryLimit_;}
    std::uint64_t memory_usage()const{return context_->node_max()*sizeof(m_tetris::TetrisNode)+(nodes_.size()-local_.free_count)*sizeof(Node);}
    void update(){local_.is_complete=false;root_->update_version(&local_);}
    RunResult run(const m_tetris::TetrisMap&map,const m_tetris::TetrisNode*node,const char*next,std::size_t length,int ms=100){return run_impl<false>(map,node,' ',false,next,length,ms);}
    RunResult run_hold(const m_tetris::TetrisMap&map,const m_tetris::TetrisNode*node,char held,bool free,const char*next,std::size_t length,int ms=100){return run_impl<true>(map,node,held,free,next,length,ms);}
};
}
#endif
