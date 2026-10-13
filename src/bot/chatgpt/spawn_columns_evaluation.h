#ifndef TETR_BOT_CHATGPT_SPAWN_COLUMNS_EVALUATION_H
#define TETR_BOT_CHATGPT_SPAWN_COLUMNS_EVALUATION_H
#include "evaluation.h"
namespace Bot_chatgpt {
// The normal spawn footprint is derived from every supported family. Use its
// column heights for pressure; outer stacks still pay all original terrain
// costs. Actual spawn/HOLD death checks and unknown-garbage offsets are intact.
class SpawnColumnsEvaluation : public Evaluation {
    unsigned spawnColumns_=0;
public:
    void init(const m_tetris::TetrisContext*context){
        Evaluation::init(context);spawnColumns_=0;
        for(char family:std::string("IJLOSTZ")){
            const auto*node=context->generate(family);
            for(int y=0;y<node->height;++y)spawnColumns_|=node->data[y];
        }
    }
    Status get(Landing&landing,const Result&evaluation,std::size_t depth,
               const Status&previous,const m_tetris::TetrisContext::Env&env)const{
        if(previous.dead)return previous;
        auto result=Evaluation::get(landing,evaluation,depth,previous,env);
        const auto&map=*evaluation.shape.map;int centralHeight=0;
        for(int x=0;x<map.width;++x)if(spawnColumns_&(1u<<x))centralHeight=std::max(centralHeight,int(map.top[x]));
        const int offset=result.risen+std::min(8,result.incoming)-12;
        const double oldDanger=std::max(0,map.roof+offset),newDanger=std::max(0,centralHeight+offset);
        const double difference=oldDanger*oldDanger-newDanger*newDanger;
        const double pathRefund=std::pow(.96,static_cast<double>(depth))*35*difference;
        result.pathRisk-=pathRefund;result.value+=pathRefund+95*difference;
        return result;
    }
};
}
#endif
