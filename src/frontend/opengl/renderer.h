#ifndef TETR_OPENGL_RENDERER_H
#define TETR_OPENGL_RENDERER_H
#include <glad/gl.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <array>
#include <map>
#include <string>
#include <vector>
#include "tetr/frontend/ui_surface.h"

namespace Graphical {
struct Color {float r,g,b,a=1;};
inline constexpr Color cyan{.23f,.87f,.94f}, purple{.65f,.47f,1},
    textColor{.87f,.93f,1}, muted{.48f,.59f,.72f}, panelColor{.035f,.065f,.11f};
class Renderer {
public:
    explicit Renderer(const std::string& font);
    ~Renderer();
    void begin(int width,int height);
    void rect(float x,float y,float w,float h,Color color);
    void outline(float x,float y,float w,float h,Color color,float thickness=1);
    void text(float x,float y,const std::string& value,float size=20,Color color=textColor);
    void surface(const Frontend::UiFrame& frame);
    void end();
    void screenshot(const std::string& path,int width,int height);
private:
    struct Vertex {float x,y,u,v,r,g,b,a;};
    struct Glyph {float x,y,w,h,left,top,advance;};
    Glyph glyph(char32_t code);
    void quad(float x,float y,float w,float h,float u,float v,float uw,float vh,Color color);
    GLuint program_=0,vao_=0,vbo_=0,atlas_=0;
    FT_Library ft_=nullptr;
    FT_Face face_=nullptr;
    std::map<char32_t,Glyph> glyphs_;
    int atlasX_=2,atlasY_=2,atlasRow_=0;
    std::vector<Vertex> vertices_;
};
Color cell_color(int id,bool obscured=false);
}
#endif
