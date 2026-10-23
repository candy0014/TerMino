#ifndef TETR_OPENGL_RENDERER_H
#define TETR_OPENGL_RENDERER_H
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "tetr/frontend/ui_surface.h"
#include "event_motion.h"
#include "field_effects.h"
#include "interaction.h"
#include "menu_viewport.h"

namespace Graphical {
struct SessionView;
struct Color {float r,g,b,a=1;};
// Match the terminal Orange and Accent roles. The surrounding neutrals stay
// warm and dark; piece/status colors retain their semantic meaning.
constexpr Color rgb(unsigned value) {
    return {float(value>>16&255)/255,float(value>>8&255)/255,float(value&255)/255};
}
inline constexpr Color accent=rgb(0xED6D00), secondary=rgb(0xC1A841),
    textColor=rgb(0xEEE8DF), muted=rgb(0xA69A8D), panelColor=rgb(0x191613),
    borderColor=rgb(0x44362B), raisedColor=rgb(0x241D17), focusColor=rgb(0x392516),
    backgroundTop=rgb(0x16110D), backgroundBottom=rgb(0x090807),
    wellColor=rgb(0x0A0908), gridColor=rgb(0x151310);
class Renderer {
public:
    explicit Renderer(const std::string& font);
    ~Renderer();
    void begin(int width,int height,double animationTime=-1,bool fullWindow=false);
    float scene_left() const {return scaledScene_?(*scaledScene_)[0]:-sceneOffset_;}
    float scene_right() const {return scaledScene_?(*scaledScene_)[1]:1200+sceneOffset_;}
    float ui_width() const {return uiWidth_;}
    void touch_controls(bool enabled) {touchControls_=enabled;}
    bool touch_controls() const {return touchControls_;}
    void minimum_text_size(float size) {minimumTextSize_=std::max(0.f,size);}
    float text_size(float size) const {return std::max(size,minimumTextSize_);}
    const BoardEvents& events(int slot,const Presentation::BoardView& board,const SessionView& session);
    const FieldEffects& field_effects(int slot,const Presentation::BoardView& board,const SessionView& session);
    void reset_event_animations() {eventStates_.clear();fieldEffects_.clear();}
    void text_effect(const TextMotion& motion,float anchorX,float anchorY,const std::function<void()>& draw);
    void board_effect(float centerX,float centerY,float unit,const FieldTransform& motion,const std::function<void()>& draw);
    void additive(const std::function<void()>& draw);
    void scaled(float scale,float centerX,float centerY,const std::function<void()>& draw);
    void glow(float x,float y,float radiusX,float radiusY,Color color);
    void shard(float x,float y,float size,float angle,Color color);
    void rect(float x,float y,float w,float h,Color color);
    void rule(float x,float y,float width,Color color=borderColor);
    void outline(float x,float y,float w,float h,Color color,float thickness=1);
    void gradient(float x,float y,float w,float h,Color top,Color bottom);
    void rounded(float x,float y,float w,float h,float radius,Color color);
    void line(float x,float y,float endX,float endY,Color color,float thickness=1);
    void polygon(const std::vector<std::array<float,2>>& points,Color color);
    void mino(float x,float y,float size,Color color,bool ghost=false,int style=0);
    void mino(Rect bounds,Color color,bool ghost=false,int style=0);
    void text(float x,float y,const std::string& value,float size=20,Color color=textColor);
    // Condensed, tracked action text. Right edge and baseline stay fixed;
    // return the rendered advance for adjacent labels with another size.
    float hud_text(float right,float baseline,const std::string& value,float size,Color color,
        float width,float tracking=0,float condensed=.8f);
    // Center actual glyph ink, including its bearings, and fit it inside a box.
    void centered_text(Rect box,const std::string& value,float size,Color color=textColor,
        float outline=0,float slant=0);
    float measure(const std::string& value,float size=20);
    void fitted(float x,float y,const std::string& value,float width,float size=20,Color color=textColor);
    std::vector<std::string> wrap(const std::string& value,float width,float size);
    float paragraph(float x,float y,const std::string& value,float width,float size=16,Color color=muted,int maxLines=8);
    void controls(const std::vector<Frontend::UiEntry>& entries,const std::string& notice={},
        const std::string& title="CONTROLS");
    void clip(std::optional<Rect> bounds) {clip_=bounds;}
    void surface(const Frontend::UiFrame& frame);
    // Reflow a semantic page into a pane, uniformly scaling its hit targets too.
    void surface_in(const Frontend::UiFrame& frame,Rect pane,float zoom=1,float top=0);
    void ui_target(const Frontend::UiMetadata& metadata,UiHit hit) {hitMetadata_=metadata;hits_.push_back(std::move(hit));}
    void input_target(Rect bounds) {inputRect_=bounds;}
    std::optional<UiHit> hit(float x,float y,const Frontend::UiMetadata& current) const;
    Interaction interaction() const {return {hitMetadata_,hits_,inputRect_,editorField_,scrollAreas_,menuScrollAreas_,menuScrollMetrics_};}
    void scroll_menu(std::vector<MenuScroll> movements) {menuScrollEvents_=std::move(movements);}
    std::optional<Rect> text_input_rect() const {return inputRect_;}
    void end();
    void pointer(std::optional<std::array<float,2>> position) {pointer_=position;}
    void screenshot(const std::string& path,int width,int height);
private:
    struct Vertex {float x,y,u,v,r,g,b,a;};
    struct Glyph {float x,y,w,h,left,top,advance,em;};
    Glyph glyph(char32_t code,float size=20);
    void draw_text(float x,float y,const std::string& value,float size,Color color);
    float scroll_viewport(const Frontend::UiMetadata& metadata,const std::string& key,
        std::size_t selected,std::size_t count,std::size_t visible,Rect bounds,float cellSize,bool horizontal=false);
    void quad(float x,float y,float w,float h,float u,float v,float uw,float vh,Color color);
    bool menu(const Frontend::UiMetadata& metadata);
    bool application_surface(const Frontend::UiFrame& frame);
    GLuint program_=0,vao_=0,vbo_=0,atlas_=0;
    FT_Library ft_=nullptr;
    FT_Face face_=nullptr;
    std::map<std::uint64_t,Glyph> glyphs_;
    float viewportScale_=1;
    float sceneOffset_=0;
    std::optional<std::array<float,2>> scaledScene_;
    float uiWidth_=1200;
    float minimumTextSize_=0;
    bool touchControls_=false;
    Glyph fallback_{0,0,0,0,0,0,0,20};
    int atlasX_=2,atlasY_=70,atlasRow_=0;
    std::vector<Vertex> vertices_;
    std::vector<std::pair<std::size_t,std::size_t>> additiveRanges_;
    std::optional<Rect> clip_,inputRect_;
    std::vector<UiHit> hits_;
    std::optional<EditorFieldHit> editorField_;
    std::vector<std::pair<Rect,std::size_t>> scrollAreas_;
    std::optional<Frontend::UiMetadata> hitMetadata_;
    std::map<std::string,MenuViewport> menuScroll_;
    std::vector<std::pair<Rect,std::string>> menuScrollAreas_;
    std::vector<MenuScrollMetrics> menuScrollMetrics_;
    std::vector<MenuScroll> menuScrollEvents_;
    std::map<int,BoardEvents> eventStates_;
    std::map<int,FieldEffects> fieldEffects_;
    double animationTime_=0;
    std::optional<std::array<float,2>> pointer_;
    std::optional<Rect> hoverBounds_;
    std::uint64_t hoverPage_=0;
    double hoverTime_=0;
    float hoverAmount_=0;
};
Color cell_color(int id,bool obscured=false);
}
#endif
