#ifndef TETR_BOT_CHATGPT_TRANSPOSITION_ENGINE_H
#define TETR_BOT_CHATGPT_TRANSPOSITION_ENGINE_H

// Private adapter of the MIT-licensed TetrisAI native widening schedule.
// Reuse its public tree/heap types without changing the shared opponent code.
#include "evaluation.h"
#include "rule_tetr_srs_plus.h"
#include <array>
#include <chrono>
#include <memory>
#include <stdexcept>

namespace Bot_chatgpt {
class TranspositionEngine {
    friend struct TranspositionTestAccess;
    using Native=m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule,Evaluation,Search>;
    using Node=Native::TreeNode;
    using Builder=Native::ContextBuilder;
    using Clock=std::chrono::steady_clock;
    using Key=std::array<std::uint32_t,91>;
    struct Entry {
        Key key{};
        double reward=0, risk=0;
        std::uint64_t generation=0;
    };
    static constexpr std::size_t tableSize=4096;
    std::unique_ptr<std::array<Entry,tableSize>> table_=std::make_unique<std::array<Entry,tableSize>>();
    std::uint64_t generation_=1;
    std::deque<Node> nodes_;
    std::shared_ptr<m_tetris::TetrisContext> context_;
    Builder::LocalContext<Node> local_{&nodes_};
    Node* root_=nullptr;
    Evaluation ai_;
    Search search_;
    Evaluation::Status status_;
    std::uint64_t memoryLimit_=64ull<<20;
    bool enabled_=true;

    void bind() {
        local_.engine=context_.get();local_.ai=&ai_;local_.search=&search_;
        Builder::init_ai(ai_,&local_,context_.get());
        Builder::init_search(search_,&local_,context_.get());
    }
    void reset_table() {
        if(++generation_==0) {for(auto&e:*table_)e.generation=0;++generation_;}
        stats_={};
    }
    Key key(const Node* node) const {
        Key result{};
        std::copy_n(node->map.row,40,result.begin());
        const auto* parent=node->parent;
        auto next=parent->next;
        // Derive the future active/HOLD exactly as search_children does;
        // node->next/node->hold are assigned lazily only on expansion.
        if(node->is_hold&&parent->hold==' '&&next!=local_.next.end())++next;
        const char held=node->is_hold?parent->node:parent->hold;
        const auto&s=node->status.get_raw();
        result[40]=parent->level+1;
        result[41]=std::distance(local_.next.cbegin(),decltype(local_.next.cbegin())(next));
        result[42]=static_cast<unsigned char>(held);
        result[43]=s.combo;result[44]=s.b2b;result[45]=s.garbageHeight;
        result[46]=s.incoming;result[47]=s.readyIncoming;result[48]=s.risen;
        result[49]=s.previousCleared;result[50]=s.placedPieces;
        if(s.attackRules.halfClearEnabled) std::copy(s.pieceRows.begin(),s.pieceRows.end(),result.begin()+51);
        return result;
    }
    static std::size_t hash(const Key& key) {
        std::uint64_t h=1469598103934665603ull;
        for(auto x:key)h=(h^x)*1099511628211ull;
        return h&(tableSize-1);
    }
    bool dominated(Node* node) {
        const auto&s=node->status.get_raw();
        if(!enabled_||s.dead)return false;
        const auto k=key(node);auto&entry=(*table_)[hash(k)];
        ++stats_.lookups;
        if(entry.generation==generation_&&entry.key==k) {
            ++stats_.equivalent;
            // Componentwise dominance avoids rounding/cancellation arguments
            // involving reward-risk. Future reward and risk additions are the
            // same from an equal state at the same depth.
            if(entry.reward>=s.reward&&entry.risk<=s.pathRisk) {++stats_.pruned;return true;}
            if(entry.reward-entry.risk>s.reward-s.pathRisk)return false;
        }
        entry={k,s.reward,s.pathRisk,generation_};
        return false;
    }
    template<bool Hold> bool pass() {
        if(local_.is_complete)return true;
        ++stats_.passes;
        if(local_.width==0) {
            auto&wait=local_.wait.back();
            for(auto*node=root_->template build_children<Hold>(&local_);node;node=node->children_next)wait.push(node);
            local_.width=2;
        }else ++local_.width;
        bool complete=true;
        std::size_t remaining=local_.max_length;
        double factor=1;
        if(remaining>0) {
            const double ratio=ai_.ratio();
            while(remaining>local_.width_cache.size())local_.width_cache.emplace_back(std::pow(local_.width_cache.size()+2,ratio));
            factor=2/ *std::max_element(local_.width_cache.begin(),local_.width_cache.end());
        }
        while(remaining-->0) {
            const auto quota=std::max<std::size_t>(1,std::size_t(local_.width_cache[remaining]*local_.width*factor));
            auto&wait=local_.wait[remaining+1];
            if(wait.empty())continue;
            complete=false;
            auto&expanded=local_.sort[remaining+1];auto&next=local_.wait[remaining];
            const auto push_one=[&] {
                while(!wait.empty()) {
                    auto*node=wait.top();wait.pop();
                    if(dominated(node))continue;
                    expanded.push(node);++stats_.expansions;
                    for(auto*child=node->template build_children<Hold>(&local_);child;child=child->children_next)next.push(child);
                    return;
                }
            };
            if(expanded.size()>=quota) {
                if(expanded.top()->status.get()<wait.top()->status.get())push_one();
            }else do {push_one();}while(expanded.size()<quota&&!wait.empty());
        }
        if(complete)local_.is_complete=true;
        return complete;
    }
    template<bool Hold> typename Native::RunResult run_impl(const m_tetris::TetrisMap& map,
        const m_tetris::TetrisNode* node,char held,bool holdFree,const char*next,std::size_t length,int ms) {
        if(!context_||!node||!node->check(map))return {};
        const auto deadline=Clock::now()+std::chrono::milliseconds(std::max(0,ms));
        const auto oldVersion=local_.version;
        if constexpr(Hold)root_=root_->update(&local_,map,status_,node,held,!holdFree,next,length);
        else root_=root_->update(&local_,map,status_,node,next,length);
        if(oldVersion!=local_.version)reset_table();
        // Match the original timed overload's stopping rule. memory_limit is
        // retained as its configured property; memory_usage also reports the
        // bounded table. The native timed overload does not enforce that cap.
        do {if(pass<Hold>())break;}while(Clock::now()<deadline);
        if constexpr(Hold) {
            if(root_->hold==' '&&local_.next.size()==1&&!root_->is_hold_lock)return typename Native::RunResult(true);
        }
        const auto best=root_->get_best(&local_);
        return best.first?typename Native::RunResult(best,Hold&&best.first->is_hold):typename Native::RunResult(false);
    }
public:
    using Status=Evaluation::Status;
    using RunResult=Native::RunResult;
    struct Statistics {std::size_t passes=0,expansions=0,lookups=0,equivalent=0,pruned=0;};
private:
    Statistics stats_;
public:
    explicit TranspositionEngine(std::shared_ptr<m_tetris::TetrisContext> context={},bool enabled=true)
        :context_(std::move(context)),enabled_(enabled) {
        nodes_.emplace_back();root_=&nodes_.back();
        if(context_) {
            if(!m_tetris::TetrisRuleInit<rule_tetr_srs_plus::TetrisRule>::init(context_->width(),context_->height()))throw std::runtime_error("Invalid geometry");
            bind();
        }
    }
    TranspositionEngine(const TranspositionEngine&)=delete;
    TranspositionEngine&operator=(const TranspositionEngine&)=delete;
    bool prepare(int width,int height) {
        if(context_&&context_->width()==width&&context_->height()==height)return true;
        Native geometry;if(!geometry.prepare(width,height))return false;
        context_=geometry.context();bind();return true;
    }
    const auto&context()const{return context_;}
    Status*status(){return &status_;}
    Evaluation*ai(){return &ai_;}
    auto*search_config(){return local_.search_config();}
    void memory_limit(std::uint64_t bytes){memoryLimit_=std::clamp<std::uint64_t>(bytes,32ull<<20,2ull<<30);}
    std::uint64_t memory_limit()const{return memoryLimit_;}
    std::uint64_t memory_usage()const{return context_->node_max()*sizeof(m_tetris::TetrisNode)+(nodes_.size()-local_.free_count)*sizeof(Node)+sizeof(*table_);}
    static constexpr std::size_t table_bytes(){return sizeof(std::array<Entry,tableSize>);}
    const Statistics&statistics()const{return stats_;}
    void update(){local_.is_complete=false;root_->update_version(&local_);reset_table();}
    RunResult run(const m_tetris::TetrisMap&map,const m_tetris::TetrisNode*node,const char*next,std::size_t length,int ms=100){return run_impl<false>(map,node,' ',false,next,length,ms);}
    RunResult run_hold(const m_tetris::TetrisMap&map,const m_tetris::TetrisNode*node,char held,bool free,const char*next,std::size_t length,int ms=100){return run_impl<true>(map,node,held,free,next,length,ms);}
};
} // namespace Bot_chatgpt
#endif
