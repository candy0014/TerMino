#ifndef TETR_OPENGL_SCENE_H
#define TETR_OPENGL_SCENE_H
#include "renderer.h"
#include "tetr/frontend/graphical/session.h"
namespace Graphical {
void background(Renderer& renderer,bool header=true,float contentScale=1);
void game_view(Renderer& renderer,const SessionView& view);
void scene_view(Renderer& renderer,const SessionView& view);
}
#endif
