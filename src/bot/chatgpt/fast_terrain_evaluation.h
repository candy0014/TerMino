#include "tetr/util/bit_ops.h"
#ifndef TETR_BOT_CHATGPT_FAST_TERRAIN_EVALUATION_H
#define TETR_BOT_CHATGPT_FAST_TERRAIN_EVALUATION_H
#include "evaluation.h"
#include "integer_utils.h"
namespace Bot_chatgpt {
class FastTerrainEvaluation : public Evaluation {
public:
    Result eval(const Landing&landing,const m_tetris::TetrisMap&map,
                const m_tetris::TetrisMap&before,std::size_t clear)const {
        Result result;result.before=&before;
        result.shape={fast_native_terrain(map),static_cast<int>(clear),map.count,&map};
        const unsigned full=map.width==32?~0u:(1u<<map.width)-1;
        for(int y=0;y<landing->height;++y) {
            const int row=landing->row+y;
            if((before.row[row]|landing->data[y])==full)result.clearedRows|=std::uint64_t{1}<<row;
        }
        return result;
    }
};
}
#endif
