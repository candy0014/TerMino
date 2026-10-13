#ifndef ZZZTOJ_STANDALONE_BOT_ADAPTER_H
#define ZZZTOJ_STANDALONE_BOT_ADAPTER_H

#include "tetris_core.h"

#include <array>
#include <memory>
#include <string_view>
#include <vector>

namespace NativeBots {

enum class Kind {
    Botris,
    Toj,
    Dig,
    Qq,
    C2,
};

struct Info {
    Kind kind;
    std::string_view id;
    std::string_view displayName;
    bool supportsStrength;
    bool supportsHold;
    int maxStrength;
};

inline constexpr std::array<Info, 5> kBots{{
    {Kind::Botris, "botris", "ZZZ Botris v0.8", true, true, 20},
    {Kind::Toj, "toj", "ZZZ TOJ v0.8", true, true, 20},
    {Kind::Dig, "dig", "ZZZ Dig v0.2", false, false, 1},
    {Kind::Qq, "qq", "AX Attack v0.1", true, false, 10},
    {Kind::C2, "c2", "C2 v0.1", true, false, 20},
}};

const Info& info(Kind kind);
bool parse_kind(std::string_view text, Kind& result);

struct ChainState {
    int combo = 0;
    int b2b = 0;
};

struct Decision {
    const m_tetris::TetrisNode* target = nullptr;
    std::vector<char> path;
    bool changeHold = false;
    bool reportedSpinKnown = false;
    bool reportedSpin = false;
};

class Adapter {
public:
    virtual ~Adapter() = default;

    virtual const Info& bot_info() const noexcept = 0;
    virtual const std::shared_ptr<m_tetris::TetrisContext>& context() const noexcept = 0;
    virtual Decision choose(const m_tetris::TetrisMap& map,
                            char current,
                            char hold,
                            const char* next,
                            std::size_t nextLength,
                            bool allowHold,
                            ChainState chain,
                            int strength,
                            int searchUnitMs) = 0;
};

std::unique_ptr<Adapter> make_adapter(Kind kind, int width, int height);

} // namespace NativeBots

#endif
