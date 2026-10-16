#ifndef TETR_ENGINE_FINESSE_H
#define TETR_ENGINE_FINESSE_H
#include "tetr/runtime/piece.h"
#include "tetr/runtime/runtime_config.h"
namespace Engine {
int minimal_input_count(const Runtime::Playfield& field,const Piece::Definition& piece,Piece::Pose landing,const Runtime::MovementRestrictions& restrictions);
}
#endif
