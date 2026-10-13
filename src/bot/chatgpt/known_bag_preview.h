#ifndef TETR_BOT_CHATGPT_KNOWN_BAG_PREVIEW_H
#define TETR_BOT_CHATGPT_KNOWN_BAG_PREVIEW_H

#include <string>
#include <vector>

namespace Bot_chatgpt {

// Experimental, opt-in deduction at the visible preview's bag endpoint.
// A singleton complement fixes exactly one further piece. An empty complement
// reaches the next bag, whose order remains unknown; larger sets have no known
// order. The caller has already checked the supported standard game rules.
// This API has no player queue, random generator, HOLD or opponent access.
// Call once on a freshly constructed preview, never on the visible root next.
inline int append_known_bag_preview(std::string& preview,
                                    const std::vector<int>& unseenBagPieces,
                                    bool standardBag) {
    if (!standardBag || unseenBagPieces.size() != 1) return -1;
    const int inferred = unseenBagPieces.front();
    constexpr char pieces[] = "IJLOSTZ";
    if (inferred < 0 || inferred >= 7) return -1;
    preview.push_back(pieces[inferred]);
    return inferred;
}

} // namespace Bot_chatgpt

#endif
