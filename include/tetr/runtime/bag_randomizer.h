#ifndef TETR_RUNTIME_BAG_RANDOMIZER_H
#define TETR_RUNTIME_BAG_RANDOMIZER_H

#include <array>
#include <algorithm>
#include <string_view>
#include <vector>

namespace Runtime {
// Preserve the historical Random=0/1 meanings in settings and recordings.
enum class BagRandomizer : int {
    SevenBag=0, TotalMayhem=1, FourteenBag=2, SevenPlusOne=3,
    SevenPlusTwo=4, SevenPlusX=5, Classic=6, Pairs=7
};
struct BagRandomizerInfo {
    BagRandomizer value;
    std::string_view label;
    std::string_view tetrioName;
    std::string_view hint;
};
inline constexpr std::array<BagRandomizerInfo,8> kBagRandomizers = {{
    {BagRandomizer::SevenBag,"7-BAG","7-bag","Keep shuffling a bag of the 7 tetrominoes."},
    {BagRandomizer::FourteenBag,"14-BAG","14-bag","Keep shuffling a bag of 2x the 7 tetrominoes."},
    {BagRandomizer::SevenPlusOne,"7+1-BAG","7+1-bag","Keep shuffling a bag of the 7 tetrominoes with one random extra tetromino."},
    {BagRandomizer::SevenPlusTwo,"7+2-BAG","7+2-bag","Keep shuffling a bag of the 7 tetrominoes with two random extra tetrominoes."},
    {BagRandomizer::SevenPlusX,"7+X-BAG","7+x-bag","Keep shuffling a bag of the 7 tetrominoes. The first few bags contain a few extras."},
    {BagRandomizer::Classic,"CLASSIC","classic","Random with repetition protection."},
    {BagRandomizer::Pairs,"PAIRS","pairs","Alternate between 2 tetrominoes."},
    {BagRandomizer::TotalMayhem,"TOTAL MAYHEM","total mayhem","Completely random generation."}
}};
constexpr BagRandomizer bag_randomizer_from_index(int value) noexcept {
    return value>=0 && value<=7 ? static_cast<BagRandomizer>(value) : BagRandomizer::SevenBag;
}
constexpr const BagRandomizerInfo& bag_randomizer_info(BagRandomizer mode) noexcept {
    for(const auto& info:kBagRandomizers) if(info.value==mode) return info;
    return kBagRandomizers.front();
}
struct BagRandomizerState {
    int lastGenerated=-1;
    std::vector<int> extras;
};

// The caller supplies its piece order and RNG so normal games retain mt19937,
// while interoperable matches use the official seed and shuffle sequence.
template<class Choose, class Shuffle>
std::vector<int> generate_piece_bag(BagRandomizer mode, const std::vector<int>& order,
    int bagIndex, BagRandomizerState& state, Choose choose, Shuffle shuffle) {
    if(order.empty()) return {};
    const int count=static_cast<int>(order.size());
    std::vector<int> bag;
    switch(mode) {
        case BagRandomizer::TotalMayhem:
            for(int i=0;i<count;++i) bag.push_back(order[choose(count)]);
            return bag;
        case BagRandomizer::Classic:
            for(int i=0;i<count;++i) {
                int index=choose(count+1);
                if(index==state.lastGenerated || index>=count) index=choose(count);
                state.lastGenerated=index;
                bag.push_back(order[index]);
            }
            return bag;
        case BagRandomizer::Pairs: {
            auto pair=order; shuffle(pair);
            bag={pair[0],pair[0],pair[0],pair[count>1 ? 1 : 0],pair[count>1 ? 1 : 0],pair[count>1 ? 1 : 0]};
            break;
        }
        case BagRandomizer::FourteenBag:
            bag=order; bag.insert(bag.end(),order.begin(),order.end());
            break;
        case BagRandomizer::SevenPlusOne:
        case BagRandomizer::SevenPlusTwo:
            bag=order;
            bag.push_back(order[choose(count)]);
            if(mode==BagRandomizer::SevenPlusTwo) bag.push_back(order[choose(count)]);
            break;
        case BagRandomizer::SevenPlusX: {
            static constexpr int amounts[]{3,2,1,1};
            const int extra=bagIndex>=0 && bagIndex<4 ? amounts[bagIndex] : 0;
            if(static_cast<int>(state.extras.size())<extra) { state.extras=order; shuffle(state.extras); }
            bag=order;
            // Small custom piece sets can supply fewer than three extras.
            const int available=std::min(extra,static_cast<int>(state.extras.size()));
            bag.insert(bag.end(),state.extras.begin(),state.extras.begin()+available);
            state.extras.erase(state.extras.begin(),state.extras.begin()+available);
            break;
        }
        default: bag=order; break;
    }
    shuffle(bag);
    return bag;
}
} // namespace Runtime
#endif
