#ifndef TETR_BOT_CHATGPT_TIME_B2B_EVALUATION_H
#define TETR_BOT_CHATGPT_TIME_B2B_EVALUATION_H

#include "evaluation.h"
#include <array>

namespace Bot_chatgpt {

// Single experimental change: discount terminal B2B with the original per-depth
// time factor. Evaluation stays byte-for-byte unchanged for the default build.
class TimeB2BEvaluation : public Evaluation {
public:
    std::string ai_name() const { return "bot_chatgpt v0.4-time-b2b-rc1"; }

    void set_policy(const PolicyWeights& policy) {
        Evaluation::set_policy(policy);
        terminalB2BWeight_ = policy.b2b;
    }

    Status get(Landing& landing, const Result& evaluation, std::size_t depth,
               const Status& previous, const m_tetris::TetrisContext::Env& env) const {
        if (previous.dead) return previous;
        auto result = Evaluation::get(landing, evaluation, depth, previous, env);
        result.value += (time_discount(depth) - 1.0) *
            (terminalB2BWeight_ * result.b2b + (result.b2b ? 300.0 : 0.0));
        return result;
    }

private:
    static double time_discount(std::size_t depth) {
        // Use the same pow call for each entry, not iterative multiplication.
        // Function-local storage adds no initializer to an unused default BOT.
        static const std::array<double, 41> table = [] {
            std::array<double, 41> values{};
            for (std::size_t i = 0; i < values.size(); ++i)
                values[i] = std::pow(0.96, static_cast<double>(i));
            return values;
        }();
        return depth < table.size() ? table[depth]
            : std::pow(0.96, static_cast<double>(depth));
    }

    double terminalB2BWeight_ = 480.0;
};

} // namespace Bot_chatgpt
#endif
