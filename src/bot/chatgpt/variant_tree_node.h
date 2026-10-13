#ifndef TETR_BOT_CHATGPT_VARIANT_TREE_NODE_H
#define TETR_BOT_CHATGPT_VARIANT_TREE_NODE_H
// Bot-local copy of the native tree, with a Spin-aware cache identity.
// Generated from zzztoj_standalone/upstream/src/tetris_core.h by the archived
// variant_cache_generate.py. Widening, ordering, scoring, and rules are unchanged.
#include "search.h"
namespace Bot_chatgpt::variant_cache_detail {
using namespace m_tetris;
struct VariantKey {
    std::uint64_t value;
    explicit VariantKey(const Landing& move) : value(std::uint64_t(move->status.status)
        | (std::uint64_t(move.spin) << 32) | (std::uint64_t(move.mini) << 33)) {}
};
struct VariantKeyHash { std::size_t operator()(const VariantKey& key) const { return key.value; } };
struct VariantKeyEqual { bool operator()(const VariantKey& a, const VariantKey& b) const { return a.value == b.value; } };
    template<class Status, class TetrisAI, class TetrisSearch>
    struct VariantTreeNode
    {
        typedef TetrisCore<TetrisAI, TetrisSearch> Core;
        struct Context
        {
        public:
            struct ValueHeapCompare
            {
                bool operator()(VariantTreeNode *left, VariantTreeNode *right) const
                {
                    return left->status.get() < right->status.get();
                }
            };
            template<class, class>
            struct TetrisNext
            {
                TetrisNext(char _node) : node(_node), vp()
                {
                }
                void set_vp(bool _vp)
                {
                    vp = _vp;
                }
                bool get_vp()
                {
                    return vp;
                }
                bool operator == (TetrisNext const &other) const
                {
                    return node == other.node && vp == other.vp;
                }
                operator char() const
                {
                    return node;
                }
                char node;
                bool vp;
            };
            template<class Unuse>
            struct TetrisNext<Unuse, std::false_type>
            {
                TetrisNext(char _node) : node(_node)
                {
                }
                void set_vp(bool)
                {
                }
                bool get_vp()
                {
                    return false;
                }
                bool operator == (TetrisNext const &other) const
                {
                    return node == other.node;
                }
                operator char() const
                {
                    return node;
                }
                char node;
            };
            typedef TetrisNext<TetrisAI, typename TetrisAIHasIterate<TetrisAI>::type> next_t;
        public:
            Context(std::deque<VariantTreeNode> *_node_storage) : version(), is_complete(), is_open_hold(), width(), node_storage(_node_storage), free_list(nullptr), free_count(0), total(), avg()
            {
            }
        public:
            typedef std::priority_queue<VariantTreeNode *, std::vector<VariantTreeNode *>, ValueHeapCompare> value_heap_t;
            typedef chash_map<VariantKey, VariantTreeNode *, VariantKeyHash, VariantKeyEqual> children_map_t;
            typedef chash_set<VariantKey, VariantKeyHash, VariantKeyEqual> identity_set_t;
            size_t version;
            TetrisContext const *engine;
            TetrisAI *ai;
            TetrisSearch *search;
            std::vector<value_heap_t> sort;
            std::vector<value_heap_t> wait;
            children_map_t old;
            identity_set_t uniq;
            bool is_complete;
            bool is_open_hold;
            bool is_virtual;
            bool unused_bool;
            size_t max_length;
            size_t width;
            std::deque<VariantTreeNode> *node_storage;
            VariantTreeNode* free_list;
            size_t free_count;
            std::vector<Status const *> iterate_cache;
            TetrisNode virtual_flag;
            TetrisNode const *current;
            std::vector<next_t> next;
            std::vector<char> next_c;
            std::vector<double> width_cache;
            double total;
            double avg;
        public:
            VariantTreeNode *alloc(VariantTreeNode *parent)
            {
                VariantTreeNode *node;
                if (free_list != nullptr)
                {
                    node = free_list;
                    free_list = free_list->parent;
                    node->version = version - 1;
                    --free_count;
                }
                else
                {
                    node_storage->emplace_back();
                    node = &node_storage->back();
                }
                node->parent = parent;
                return node;
            }
            void dealloc(VariantTreeNode *node)
            {
                for (auto it = node->children; it != nullptr; it = it->children_next)
                {
                    dealloc(it);
                }
                node->children = nullptr;
                node->node_flag.clear();
                node->flag = 0;
                node->node = ' ';
                node->hold = ' ';
                node->level = 1;
                node->parent = free_list;
                free_list = node;
                ++free_count;
            }
        };
        struct TetrisNodeFlag
        {
            TetrisNode const *flag[2];
            TetrisNodeFlag()
            {
                clear();
            }
            bool empty()
            {
                return flag[0] == nullptr;
            }
            bool check(TetrisNode const *node1)
            {
                assert(node1 != nullptr);
                return flag[0] == node1 && flag[1] == nullptr;
            }
            bool check(TetrisNode const *node1, TetrisNode const *node2)
            {
                assert(node1 != nullptr);
                assert(node2 != nullptr);
                return flag[0] == node1 && flag[1] == node2;
            }
            void set(TetrisNode const *node1)
            {
                assert(node1 != nullptr);
                flag[0] = node1;
                flag[1] = nullptr;
            }
            void set(TetrisNode const *node1, TetrisNode const *node2)
            {
                assert(node1 != nullptr);
                assert(node2 != nullptr);
                flag[0] = node1;
                flag[1] = node2;
            }
            void clear()
            {
                flag[0] = nullptr;
                flag[1] = nullptr;
            }
        };
        template<class, class>
        struct TreeNodeStatus
        {
            Status status_raw;
            Status status;
            Status const &get() const
            {
                return status;
            }
            Status const &get_raw() const
            {
                return status_raw;
            }
            void set(Status const &_status)
            {
                status = _status;
                status_raw = _status;
            }
            void set_vp(Status const &_status)
            {
                status = _status;
            }
        };
        template<class Unuse>
        struct TreeNodeStatus<Unuse, std::false_type>
        {
            Status status;
            Status const &get() const
            {
                return status;
            }
            Status const &get_raw() const
            {
                return status;
            }
            void set(Status const &_status)
            {
                status = _status;
            }
            void set_vp(Status const &_status)
            {
                status = _status;
            }
        };
        typedef typename Context::next_t next_t;
        VariantTreeNode() : flag(), node(' '), hold(' '), level(1), version(-1), identity(), parent(), children()
        {
        }
        union
        {
            struct
            {
                uint8_t flag;
                char node;
                char hold;
                uint8_t level;
            };
            struct
            {
                uint8_t is_dead : 1;
                uint8_t is_hold : 1;
                uint8_t is_hold_lock : 1;
                uint8_t is_virtual : 1;
            };
        };
        size_t version;
        TetrisMap map;
        typename Core::LandPoint identity;
        typename Core::Result result;
        TreeNodeStatus<TetrisAI, typename TetrisAIHasIterate<TetrisAI>::type> status;
        VariantTreeNode *parent;
        VariantTreeNode *children;
        VariantTreeNode *children_next;
        TetrisNodeFlag node_flag;
        typename std::vector<next_t>::const_iterator next;

        VariantTreeNode *update_root(Context *context, TetrisMap const &_map)
        {
            if (map == _map)
            {
                return this;
            }
            VariantTreeNode *new_root = nullptr;
            for (VariantTreeNode *it = children, *last = nullptr; it != nullptr; last = it, it = it->children_next)
            {
                if (it->map == _map)
                {
                    new_root = it;
                    (last == nullptr ? children : last->children_next) = it->children_next;
                    break;
                }
            }
            if (new_root == nullptr)
            {
                new_root = context->alloc(nullptr);
                new_root->map = _map;
            }
            else
            {
                new_root->parent = nullptr;
            }
            context->dealloc(this);
            return new_root;
        }
        void update_version(Context *context)
        {
            ++context->version;
            context->total += context->width;
            context->avg = context->total / context->version;
            context->width = 0;
            context->wait.clear();
            context->sort.clear();
            context->wait.resize(context->max_length + 1);
            context->sort.resize(context->max_length + 1);
        }
        static std::vector<next_t> process_next(char const *_next, size_t _next_length, TetrisNode const *_node)
        {
            std::vector<next_t> next;
            next.push_back(_node->status.t);
            size_t length = 0;
            for (size_t i = 0; i < _next_length; ++i)
            {
                if (_next[i] == '?')
                {
                    next[length].set_vp(true);
                }
                else
                {
                    ++length;
                    next.push_back(_next[i]);
                }
            }
            return next;
        }
        VariantTreeNode *update(Context *context, TetrisMap const &_map, Status const &status, TetrisNode const *_node, char const *_next, size_t _next_length)
        {
            VariantTreeNode *root = update_root(context, _map);
            std::vector<next_t> next = process_next(_next, _next_length, _node);
            if (root != this || (context->current == nullptr || _node->status.t != root->node) || context->is_open_hold || next != context->next)
            {
                context->is_complete = false;
                context->max_length = next.size() - 1;
                update_version(context);
                context->is_open_hold = false;
                context->current = _node;
                context->next = next;
                if (Core::template EnableNextC<VariantTreeNode>::value)
                {
                    context->next_c.assign(next.begin(), next.end());
                }
                root->node = _node->status.t;
                root->next = std::next(context->next.begin());
            }
            else if (context->current != _node)
            {
                context->is_complete = false;
                ++context->version;
                context->current = _node;
            }
            root->status.set(status);
            context->width_cache.clear();
            return root;
        }
        VariantTreeNode *update(Context *context, TetrisMap const &_map, Status const &status, TetrisNode const *_node, char _hold, bool _hold_lock, char const *_next, size_t _next_length)
        {
            VariantTreeNode *root = update_root(context, _map);
            std::vector<next_t> next = process_next(_next, _next_length, _node);
            if (root != this || (context->current == nullptr || _node->status.t != root->node) || !context->is_open_hold || next != context->next || _hold != root->hold || !!_hold_lock != root->is_hold_lock)
            {
                context->is_complete = false;
                context->max_length = next.size() - 1;
                if (_hold != ' ' && (_next_length > 1 || !_hold_lock))
                {
                    ++context->max_length;
                }
                update_version(context);
                context->is_open_hold = true;
                context->current = _node;
                context->next = next;
                if (Core::template EnableNextC<VariantTreeNode>::value)
                {
                    context->next_c.assign(next.begin(), next.end());
                }
                root->node = _node->status.t;
                root->hold = _hold;
                root->is_hold = false;
                root->is_hold_lock = _hold_lock;
                root->next = std::next(context->next.begin());
            }
            else if (context->current != _node)
            {
                context->is_complete = false;
                ++context->version;
                context->current = _node;
            }
            root->status.set(status);
            context->width_cache.clear();
            return root;
        }
        void search(Context *context, TetrisNode const *search_node, bool is_hold)
        {
            if (node_flag.empty())
            {
                node_flag.set(search_node);
                for (auto land_point_node : *context->search->search(map, search_node, level))
                {
                    VariantTreeNode *child = context->alloc(this);
                    Core::eval(context, map, land_point_node, child);
                    child->is_hold = is_hold;
                    child->children_next = children;
                    children = child;
                }
            }
            else if (!node_flag.check(search_node))
            {
                node_flag.set(search_node);
                auto &old = context->old;
                for (auto it = children; it != nullptr; it = it->children_next)
                {
                    old.emplace(VariantKey(it->identity), it);
                }
                children = nullptr;
                for (auto land_point_node : *context->search->search(map, search_node, level))
                {
                    VariantTreeNode *child;
                    auto find = old.find(VariantKey(land_point_node));
                    if (find != old.end())
                    {
                        child = find->second;
                        old.erase(find);
                    }
                    else
                    {
                        child = context->alloc(this);
                        Core::eval(context, map, land_point_node, child);
                    }
                    child->is_hold = is_hold;
                    child->children_next = children;
                    children = child;
                }
                for (auto &pair : old)
                {
                    context->dealloc(pair.second);
                }
                old.clear();
            }
        }
        void search(Context *context, TetrisNode const *search_node, TetrisNode const *hold_node)
        {
            if (search_node == hold_node)
            {
                return search(context, search_node, false);
            }
            if (search_node->status.t == hold_node->status.t)
            {
                if (node_flag.empty())
                {
                    node_flag.set(search_node, hold_node);
                    auto &uniq = context->uniq;
                    for (auto land_point_node : *context->search->search(map, search_node, level))
                    {
                        VariantTreeNode *child = context->alloc(this);
                        Core::eval(context, map, land_point_node, child);
                        child->is_hold = false;
                        child->children_next = children;
                        children = child;
                        uniq.insert(VariantKey(child->identity));
                    }
                    if (children != nullptr)
                    {
                        for (auto land_point_node : *context->search->search(map, hold_node, level))
                        {
                            if (uniq.find(VariantKey(land_point_node)) != uniq.end())
                            {
                                continue;
                            }
                            VariantTreeNode *child = context->alloc(this);
                            Core::eval(context, map, land_point_node, child);
                            child->is_hold = true;
                            child->children_next = children;
                            children = child;
                        }
                    }
                    uniq.clear();
                }
                else if (!node_flag.check(search_node, hold_node))
                {
                    auto &old = context->old;
                    for (auto it = children; it != nullptr; it = it->children_next)
                    {
                        old.emplace(VariantKey(it->identity), it);
                    }
                    children = nullptr;
                    if (node_flag.check(hold_node, search_node))
                    {
                        node_flag.set(search_node, hold_node);
                        for (auto land_point_node : *context->search->search(map, search_node, level))
                        {
                            auto find = old.find(VariantKey(land_point_node));
                            assert(find != old.end());
                            VariantTreeNode *child = find->second;
                            old.erase(find);
                            child->is_hold = false;
                            child->children_next = children;
                            children = child;
                        }
                        if (children != nullptr)
                        {
                            for (auto &pair : old)
                            {
                                auto child = pair.second;
                                child->is_hold = true;
                                child->children_next = children;
                                children = child;
                            }
                        }
                        else
                        {
                            for (auto &pair : old)
                            {
                                context->dealloc(pair.second);
                            }
                        }
                    }
                    else
                    {
                        node_flag.set(search_node, hold_node);
                        auto &uniq = context->uniq;
                        for (auto land_point_node : *context->search->search(map, search_node, level))
                        {
                            VariantTreeNode *child;
                            auto find = old.find(VariantKey(land_point_node));
                            if (find != old.end())
                            {
                                child = find->second;
                                old.erase(find);
                            }
                            else
                            {
                                child = context->alloc(this);
                                Core::eval(context, map, land_point_node, child);
                            }
                            child->is_hold = false;
                            child->children_next = children;
                            children = child;
                            uniq.insert(VariantKey(child->identity));
                        }
                        if (children != nullptr)
                        {
                            for (auto land_point_node : *context->search->search(map, hold_node, level))
                            {
                                if (uniq.find(VariantKey(land_point_node)) != uniq.end())
                                {
                                    continue;
                                }
                                VariantTreeNode *child;
                                auto find = old.find(VariantKey(land_point_node));
                                if (find != old.end())
                                {
                                    child = find->second;
                                    old.erase(find);
                                }
                                else
                                {
                                    child = context->alloc(this);
                                    Core::eval(context, map, land_point_node, child);
                                }
                                child->is_hold = true;
                                child->children_next = children;
                                children = child;
                            }
                        }
                        for (auto &pair : old)
                        {
                            context->dealloc(pair.second);
                        }
                        uniq.clear();
                    }
                    old.clear();
                }
            }
            else
            {
                if (node_flag.empty())
                {
                    node_flag.set(search_node, hold_node);
                    for (auto land_point_node : *context->search->search(map, search_node, level))
                    {
                        VariantTreeNode *child = context->alloc(this);
                        Core::eval(context, map, land_point_node, child);
                        child->is_hold = false;
                        child->children_next = children;
                        children = child;
                    }
                    if (children != nullptr)
                    {
                        for (auto land_point_node : *context->search->search(map, hold_node, level))
                        {
                            VariantTreeNode *child = context->alloc(this);
                            Core::eval(context, map, land_point_node, child);
                            child->is_hold = true;
                            child->children_next = children;
                            children = child;
                        }
                    }
                }
                else if (!node_flag.check(search_node, hold_node))
                {
                    if (node_flag.check(hold_node, search_node))
                    {
                        node_flag.set(search_node, hold_node);
                        if (!context->search->search(map, search_node, level)->empty())
                        {
                            for (auto it = children; it != nullptr; it = it->children_next)
                            {
                                it->is_hold = it->identity->status.t == hold_node->status.t;
                            }
                        }
                        else
                        {
                            for (auto it = children; it != nullptr; it = it->children_next)
                            {
                                context->dealloc(it);
                            }
                            children = nullptr;
                        }
                    }
                    else
                    {
                        node_flag.set(search_node, hold_node);
                        auto &old = context->old;
                        for (auto it = children; it != nullptr; it = it->children_next)
                        {
                            old.emplace(VariantKey(it->identity), it);
                        }
                        children = nullptr;
                        for (auto land_point_node : *context->search->search(map, search_node, level))
                        {
                            VariantTreeNode *child;
                            auto find = old.find(VariantKey(land_point_node));
                            if (find != old.end())
                            {
                                child = find->second;
                                old.erase(find);
                            }
                            else
                            {
                                child = context->alloc(this);
                                Core::eval(context, map, land_point_node, child);
                            }
                            child->is_hold = false;
                            child->children_next = children;
                            children = child;
                        }
                        for (auto land_point_node : *context->search->search(map, hold_node, level))
                        {
                            VariantTreeNode *child;
                            auto find = old.find(VariantKey(land_point_node));
                            if (find != old.end())
                            {
                                child = find->second;
                                old.erase(find);
                            }
                            else
                            {
                                child = context->alloc(this);
                                Core::eval(context, map, land_point_node, child);
                            }
                            child->is_hold = true;
                            child->children_next = children;
                            children = child;
                        }
                        for (auto &pair : old)
                        {
                            context->dealloc(pair.second);
                        }
                        old.clear();
                    }
                }
            }
        }
        void search(Context *context)
        {
            if (node_flag.empty())
            {
                node_flag.set(&context->virtual_flag);
                size_t max = context->engine->type_max();
                for (size_t i = 0; i < max; ++i)
                {
                    for (auto land_point_node : *context->search->search(map, context->engine->generate(i), level))
                    {
                        VariantTreeNode *child = context->alloc(this);
                        Core::eval(context, map, land_point_node, child);
                        child->is_hold = false;
                        child->children_next = children;
                        children = child;
                    }
                }
            }
            else if (node_flag.check(&context->virtual_flag))
            {
                node_flag.set(&context->virtual_flag);
                auto  &old = context->old;
                for (auto it = children; it != nullptr; it = it->children_next)
                {
                    old.emplace(VariantKey(it->identity), it);
                }
                children = nullptr;
                size_t max = context->engine->type_max();
                for (size_t i = 0; i < max; ++i)
                {
                    for (auto land_point_node : *context->search->search(map, context->engine->generate(i), level))
                    {
                        VariantTreeNode *child;
                        auto find = old.find(VariantKey(land_point_node));
                        if (find != old.end())
                        {
                            child = find->second;
                            old.erase(find);
                        }
                        else
                        {
                            child = context->alloc(this);
                            Core::eval(context, map, land_point_node, child);
                        }
                        child->is_hold = false;
                        child->children_next = children;
                        children = child;
                    }
                }
                for (auto &pair : old)
                {
                    context->dealloc(pair.second);
                }
                old.clear();
            }
        }
        void run_virtual(Context *context)
        {
            search(context);
            auto *engine = context->engine;
            auto &iterate_cache = context->iterate_cache;
            iterate_cache.clear();
            iterate_cache.resize(context->engine->type_max(), nullptr);
            for (auto it = children; it != nullptr; it = it->children_next)
            {
                Core::template get<false>(context, it, this);
                auto &status = iterate_cache[engine->convert(it->identity->status.t)];
                if (status == nullptr || *status < it->status.get())
                {
                    status = &it->status.get();
                }
            }
            Core::iterate(*context->ai, iterate_cache.data(), iterate_cache.size(), this);
        }
        template<bool EnableEnv>
        TetrisContext::Env env(Context *context, VariantTreeNode const *tree_node)
        {
            if (EnableEnv)
            {
                TetrisContext::Env result =
                {
                    nullptr, 0, tree_node->identity->status.t, tree_node->is_hold ? node : hold
                };
                result.length = std::distance(next, context->next.cend());
                if (result.length == 0)
                {
                    result.next = nullptr;
                }
                else
                {
                    result.next = context->next_c.data() + (context->next_c.size() - result.length);
                }
                result.is_hold = tree_node->is_hold;
                return result;
            }
            else
            {
                return
                {
                    nullptr, 0, ' ', ' ', false
                };
            }
        }
        template<bool EnableHold>
        VariantTreeNode *build_children(Context *context)
        {
            if (version == context->version || is_dead)
            {
                return children;
            }
            version = context->version;
            search_children<EnableHold>(context);
            if (children == nullptr)
            {
                is_dead = true;
                return children;
            }
            for (auto it = children; it != nullptr; it = it->children_next)
            {
                Core::template get<true>(context, it, this);
            }
            if (TetrisAIHasIterate<TetrisAI>::type::value && context->next[level].get_vp())
            {
                for (auto it = children; it != nullptr; it = it->children_next)
                {
                    it->level = level + 1;
                    it->run_virtual(context);
                }
            }
            return children;
        }
        template<bool EnableHold>
        void search_children(Context *context)
        {
            if (parent == nullptr)
            {
                assert(context->current->status.t == node);
                level = 0;
                if (EnableHold)
                {
                    if (hold == ' ')
                    {
                        if (is_hold_lock || next == context->next.end())
                        {
                            search(context, context->current, false);
                        }
                        else
                        {
                            search(context, context->current, context->engine->generate(next->node));
                        }
                    }
                    else
                    {
                        if (is_hold_lock)
                        {
                            search(context, context->current, false);
                        }
                        else
                        {
                            search(context, context->current, context->engine->generate(hold));
                        }
                    }
                }
                else
                {
                    search(context, context->current, false);
                }
                return;
            }
            level = parent->level + 1;
            if (EnableHold)
            {
                if (is_hold && parent->hold == ' ')
                {
                    assert(parent->next != context->next.end());
                    next = std::next(parent->next);
                }
                else
                {
                    next = parent->next;
                }
                if (next == context->next.end())
                {
                    node = ' ';
                }
                else
                {
                    node = next->node;
                    next = std::next(next);
                }
                hold = is_hold ? parent->node : parent->hold;
                assert(node != ' ' || hold != ' ');
                if (hold == ' ')
                {
                    if (next == context->next.end())
                    {
                        search(context, context->engine->generate(node), false);
                    }
                    else
                    {
                        search(context, context->engine->generate(node), context->engine->generate(next->node));
                    }
                }
                else
                {
                    if (node == ' ')
                    {
                        search(context, context->engine->generate(hold), true);
                    }
                    else
                    {
                        search(context, context->engine->generate(node), context->engine->generate(hold));
                    }
                }
            }
            else
            {
                assert(parent->next != context->next.end());
                node = parent->next->node;
                next = std::next(parent->next);
                search(context, context->engine->generate(node), false);
            }
        }
        template<bool EnableHold>
        bool run(Context *context)
        {
            if (context->is_complete)
            {
                return true;
            }
            assert(parent == nullptr);
            if (context->width == 0)
            {
                auto &wait = context->wait.back();
                for (auto it = build_children<EnableHold>(context); it != nullptr; it = it->children_next)
                {
                    wait.push(it);
                }
                context->width = 2;
            }
            else
            {
                context->width += 1;
            }
            bool complete = true;
            size_t next_length = context->max_length;
            double div_ratio = 1;
            if (next_length > 0)
            {
                double ratio = Core::template get_ratio<VariantTreeNode>(*context->ai);
                while (next_length > context->width_cache.size())
                {
                    context->width_cache.emplace_back(std::pow(context->width_cache.size() + 2, ratio));
                }
                div_ratio = 2 / *std::max_element(context->width_cache.begin(), context->width_cache.end());
            }
            while (next_length-- > 0)
            {
                size_t level_prune_hold = std::max<size_t>(1, size_t(context->width_cache[next_length] * context->width * div_ratio));
                auto wait = &context->wait[next_length + 1];
                if (wait->empty())
                {
                    continue;
                }
                else
                {
                    complete = false;
                }
                auto sort = &context->sort[next_length + 1];
                auto next = &context->wait[next_length];
                auto push_one = [&]
                {
                    VariantTreeNode *child = wait->top();
                    wait->pop();
                    sort->push(child);
                    for (auto it = child->build_children<EnableHold>(context); it != nullptr; it = it->children_next)
                    {
                        next->push(it);
                    }
                };
                if (wait->empty())
                {
                    // do nothing
                }
                else if (sort->size() >= level_prune_hold)
                {
                    if (sort->top()->status.get() < wait->top()->status.get())
                    {
                        push_one();
                    }
                }
                else
                {
                    do
                    {
                        push_one();
                    } while (sort->size() < level_prune_hold && !wait->empty());
                }
            }
            if (complete)
            {
                context->is_complete = true;
                return true;
            }
            return false;
        }
        std::pair<VariantTreeNode const *, Status const *> get_best(Context *context)
        {
            VariantTreeNode *best = nullptr;
            for (size_t i = 0; i < context->wait.size() && i < context->sort.size(); ++i)
            {
                auto wait_best = (context->wait.size() <= i || context->wait[i].empty()) ? nullptr : context->wait[i].top();
                auto sort_best = (context->sort.size() <= i || context->sort[i].empty()) ? nullptr : context->sort[i].top();
                if (wait_best == nullptr)
                {
                    if (sort_best == nullptr)
                    {
                        continue;
                    }
                    else
                    {
                        best = sort_best;
                    }
                }
                else
                {
                    if (sort_best == nullptr)
                    {
                        best = wait_best;
                    }
                    else
                    {
                        best = sort_best->status.get() < wait_best->status.get() ? wait_best : sort_best;
                    }
                }
                break;
            }
            if (best == nullptr)
            {
                return std::make_pair(nullptr, nullptr);
            }
            auto status = &best->status.get_raw();
            while (best->parent->parent != nullptr)
            {
                best = best->parent;
            }
            return std::make_pair(best, status);
        }
    };

}
#endif
