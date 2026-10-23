#include "renderer.h"
#include "viewport.h"
#include "tetr/frontend/graphical/session.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <stdexcept>

namespace Graphical {
namespace {
constexpr int atlasSize=4096;
std::vector<unsigned char> empty_atlas() {
    std::vector<unsigned char> pixels(atlasSize*atlasSize,0);pixels[0]=255;
    // Procedural soft light, reserved above the glyph allocator. The compact
    // radial falloff avoids the old rectangular particle halos.
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        const float dx=(x-31.5f)/31.5f,dy=(y-31.5f)/31.5f,r=dx*dx+dy*dy;
        pixels[(y+2)*atlasSize+x+2]=static_cast<unsigned char>(255*std::pow(std::max(0.f,1-r),3.f));
    }
    return pixels;
}
GLuint shader(GLenum type,const char* source) {
    const auto id=glCreateShader(type);
    glShaderSource(id,1,&source,nullptr);glCompileShader(id);
    GLint ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
    if(!ok) {char log[2048]{};glGetShaderInfoLog(id,sizeof(log),nullptr,log);glDeleteShader(id);throw std::runtime_error(log);}
    return id;
}
}
Renderer::Renderer(const std::string& font) {
    try {
        if(FT_Init_FreeType(&ft_)) throw std::runtime_error("Cannot initialize FreeType");
        // Read via filesystem to support Unicode Windows paths (FT_New_Face
        // takes the CRT narrow encoding). FreeType owns no filename encoding.
        SDL_IOStream* file=SDL_IOFromFile(font.c_str(),"rb");
        if(!file) throw std::runtime_error("Font unavailable: "+font);
        size_t bytes=0;void* buffer=SDL_LoadFile_IO(file,&bytes,true);
        if(!buffer) throw std::runtime_error("Cannot load font: "+font);
        // FT_New_Memory_Face retains its buffer until FT_Done_Face.
        const auto error=FT_New_Memory_Face(ft_,static_cast<FT_Byte*>(buffer),static_cast<FT_Long>(bytes),0,&face_);
        if(error) {SDL_free(buffer);throw std::runtime_error("Invalid font: "+font);}
        face_->generic.data=buffer;
        FT_Set_Pixel_Sizes(face_,0,96);
        const char* vertex=
#ifdef __ANDROID__
            "#version 300 es\nprecision highp float;\n"
#else
            "#version 330 core\n"
#endif
R"(
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
uniform vec2 viewport;
uniform float sceneOffset;
out vec2 texcoord; out vec4 tint;
void main(){gl_Position=vec4((position.x+sceneOffset)/viewport.x*2.0-1.0,1.0-position.y/viewport.y*2.0,0,1);texcoord=uv;tint=color;})";
        const char* fragment=
#ifdef __ANDROID__
            "#version 300 es\nprecision highp float;\n"
#else
            "#version 330 core\n"
#endif
R"(
in vec2 texcoord; in vec4 tint; out vec4 outputColor;
uniform sampler2D atlas;
void main(){outputColor=vec4(tint.rgb,tint.a*texture(atlas,texcoord).r);})";
        const GLuint vs=shader(GL_VERTEX_SHADER,vertex),fs=shader(GL_FRAGMENT_SHADER,fragment);
        program_=glCreateProgram();glAttachShader(program_,vs);glAttachShader(program_,fs);glLinkProgram(program_);
        glDeleteShader(vs);glDeleteShader(fs);
        GLint ok=0;glGetProgramiv(program_,GL_LINK_STATUS,&ok);
        if(!ok) {char log[2048]{};glGetProgramInfoLog(program_,sizeof(log),nullptr,log);throw std::runtime_error(log);}
        glGenVertexArrays(1,&vao_);glGenBuffers(1,&vbo_);
        glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,vbo_);
        for(int i=0;i<3;++i) glEnableVertexAttribArray(i);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(2*sizeof(float)));
        glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(4*sizeof(float)));
        glGenTextures(1,&atlas_);glBindTexture(GL_TEXTURE_2D,atlas_);
        const auto empty=empty_atlas();
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8,atlasSize,atlasSize,0,GL_RED,GL_UNSIGNED_BYTE,empty.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        fallback_=glyph(U'?');
    } catch(...) {
        if(face_) {void* data=face_->generic.data;FT_Done_Face(face_);SDL_free(data);face_=nullptr;}
        if(ft_) {FT_Done_FreeType(ft_);ft_=nullptr;}
        if(program_) glDeleteProgram(program_);
        throw;
    }
}
Renderer::~Renderer() {
    glDeleteTextures(1,&atlas_);glDeleteBuffers(1,&vbo_);glDeleteVertexArrays(1,&vao_);glDeleteProgram(program_);
    if(face_) {void* buffer=face_->generic.data;FT_Done_Face(face_);SDL_free(buffer);}
    if(ft_) FT_Done_FreeType(ft_);
}
void Renderer::begin(int width,int height,double animationTime,bool fullWindow) {
    minimumTextSize_=0;
    touchControls_=false;
    scaledScene_.reset();
    animationTime_=animationTime>=0?animationTime:SDL_GetTicksNS()/1e9;
    vertices_.clear();additiveRanges_.clear();clip_.reset();inputRect_.reset();hits_.clear();hitMetadata_.reset();editorField_.reset();scrollAreas_.clear();
    menuScrollAreas_.clear();menuScrollMetrics_.clear();menuScrollEvents_.clear();
    if(atlasY_>atlasSize-256) {
        glyphs_.clear();atlasX_=2;atlasY_=70;atlasRow_=0;
        glBindTexture(GL_TEXTURE_2D,atlas_);
        const auto empty=empty_atlas();
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8,atlasSize,atlasSize,0,GL_RED,GL_UNSIGNED_BYTE,empty.data());
        fallback_=glyph(U'?');
    }
    const auto view=Viewport::fit(width,height,fullWindow);
    viewportScale_=view.scale;
    sceneOffset_=view.scale>0?(width/view.scale-1200)/2:0;
    glViewport(0,view.y,width,view.height);
    glClearColor(backgroundBottom.r,backgroundBottom.g,backgroundBottom.b,1);glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program_);glUniform2f(glGetUniformLocation(program_,"viewport"),1200+2*sceneOffset_,800);
    glUniform1f(glGetUniformLocation(program_,"sceneOffset"),sceneOffset_);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,atlas_);
    glUniform1i(glGetUniformLocation(program_,"atlas"),0);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
void Renderer::quad(float x,float y,float w,float h,float u,float v,float uw,float vh,Color c) {
    if(w<=0 || h<=0)return;
    if(clip_) {
        const auto b=*clip_;
        const float left=std::max(x,b.x),top=std::max(y,b.y),right=std::min(x+w,b.x+b.w),bottom=std::min(y+h,b.y+b.h);
        if(right<=left || bottom<=top)return;
        u+=(left-x)/w*uw;v+=(top-y)/h*vh;
        uw*=(right-left)/w;vh*=(bottom-top)/h;
        x=left;y=top;w=right-left;h=bottom-top;
    }
    const Vertex a{x,y,u,v,c.r,c.g,c.b,c.a},b{x+w,y,u+uw,v,c.r,c.g,c.b,c.a},
        d{x,y+h,u,v+vh,c.r,c.g,c.b,c.a},e{x+w,y+h,u+uw,v+vh,c.r,c.g,c.b,c.a};
    vertices_.insert(vertices_.end(),{a,b,d,b,e,d});
}
void Renderer::rect(float x,float y,float w,float h,Color c) {
    quad(x,y,w,h,.5f/atlasSize,.5f/atlasSize,0,0,c);
}
void Renderer::rule(float x,float y,float width,Color color) {
    const float scale=std::max(.01f,viewportScale_);
    rect(x,std::round(y*scale)/scale,width,std::max(1.f,1.f/scale),color);
}
void Renderer::outline(float x,float y,float w,float h,Color c,float t) {
    rect(x,y,w,t,c);rect(x,y+h-t,w,t,c);rect(x,y,t,h,c);rect(x+w-t,y,t,h,c);
}
void Renderer::gradient(float x,float y,float w,float h,Color top,Color bottom) {
    const auto start=vertices_.size();rect(x,y,w,h,top);
    for(auto i=start;i<vertices_.size();++i) {
        auto& v=vertices_[i];const float t=(v.y-y)/h;
        v.r=top.r+(bottom.r-top.r)*t;v.g=top.g+(bottom.g-top.g)*t;
        v.b=top.b+(bottom.b-top.b)*t;v.a=top.a+(bottom.a-top.a)*t;
    }
}
void Renderer::rounded(float x,float y,float w,float h,float radius,Color c) {
    const float r=std::min({radius,w/2,h/2});
    rect(x+r,y,w-2*r,h,c);rect(x,y+r,r,h-2*r,c);rect(x+w-r,y+r,r,h-2*r,c);
    for(int corner=0;corner<4;++corner) {
        const float cx=x+(corner==0 || corner==3?r:w-r),cy=y+(corner<2?r:h-r);
        for(int i=0;i<8;++i) {
            const float a=(corner*90+180+i*90.f/8)*3.14159265f/180;
            const float b=a+3.14159265f/16;
            const auto vertex=[&](float px,float py) {return Vertex{px,py,.5f/atlasSize,.5f/atlasSize,c.r,c.g,c.b,c.a};};
            if(clip_)polygon({{cx,cy},{cx+r*std::cos(a),cy+r*std::sin(a)},{cx+r*std::cos(b),cy+r*std::sin(b)}},c);
            else vertices_.insert(vertices_.end(),{vertex(cx,cy),vertex(cx+r*std::cos(a),cy+r*std::sin(a)),vertex(cx+r*std::cos(b),cy+r*std::sin(b))});
        }
    }
}
void Renderer::line(float x,float y,float ex,float ey,Color c,float thickness) {
    if(clip_) {
        const auto b=*clip_;const float dx=ex-x,dy=ey-y;float clipStart=0,clipEnd=1;
        const auto edge=[&](float p,float q) {
            if(p==0)return q>=0;
            const float t=q/p;if(p<0)clipStart=std::max(clipStart,t);else clipEnd=std::min(clipEnd,t);
            return clipStart<=clipEnd;
        };
        if(!edge(-dx,x-b.x) || !edge(dx,b.x+b.w-x) || !edge(-dy,y-b.y) || !edge(dy,b.y+b.h-y))return;
        ex=x+dx*clipEnd;ey=y+dy*clipEnd;x+=dx*clipStart;y+=dy*clipStart;
    }
    const float length=std::hypot(ex-x,ey-y);if(length<=0)return;
    const float dx=(ey-y)/length*thickness/2,dy=(x-ex)/length*thickness/2;
    const auto v=[&](float px,float py) {return Vertex{px,py,.5f/atlasSize,.5f/atlasSize,c.r,c.g,c.b,c.a};};
    vertices_.insert(vertices_.end(),{v(x-dx,y-dy),v(x+dx,y+dy),v(ex-dx,ey-dy),v(ex-dx,ey-dy),v(x+dx,y+dy),v(ex+dx,ey+dy)});
}
void Renderer::glow(float x,float y,float rx,float ry,Color c) {
    quad(x-rx,y-ry,2*rx,2*ry,2.f/atlasSize,2.f/atlasSize,64.f/atlasSize,64.f/atlasSize,c);
}
void Renderer::polygon(const std::vector<std::array<float,2>>& points,Color c) {
    // Convex solid geometry, clipped before the board's rigid transform.
    auto clipped=points;
    if(clip_)for(int edge=0;edge<4 && !clipped.empty();++edge) {
        const int axis=edge/2;const bool lower=edge%2==0;
        const float bound=axis==0?(lower?clip_->x:clip_->x+clip_->w):(lower?clip_->y:clip_->y+clip_->h);
        const auto inside=[&](auto p){return lower?p[axis]>=bound:p[axis]<=bound;};
        std::vector<std::array<float,2>> output;auto previous=clipped.back();
        for(const auto current:clipped) {
            if(inside(previous)!=inside(current)) {
                const float t=(bound-previous[axis])/(current[axis]-previous[axis]);
                output.push_back({previous[0]+t*(current[0]-previous[0]),previous[1]+t*(current[1]-previous[1])});
            }
            if(inside(current))output.push_back(current);previous=current;
        }
        clipped=std::move(output);
    }
    const auto vertex=[&](auto p){return Vertex{p[0],p[1],.5f/atlasSize,.5f/atlasSize,c.r,c.g,c.b,c.a};};
    for(std::size_t i=1;i+1<clipped.size();++i)
        vertices_.insert(vertices_.end(),{vertex(clipped[0]),vertex(clipped[i]),vertex(clipped[i+1])});
}
void Renderer::shard(float x,float y,float size,float angle,Color c) {
    const float dx=std::cos(angle)*size/2,dy=std::sin(angle)*size/2;
    const auto v=[&](float px,float py,float light) {return Vertex{px,py,.5f/atlasSize,.5f/atlasSize,c.r*light,c.g*light,c.b*light,c.a};};
    const auto a=v(x-dx+dy,y-dy-dx,1),b=v(x+dx+dy,y+dy-dx,.8f),d=v(x-dx-dy,y-dy+dx,.65f),e=v(x+dx-dy,y+dy+dx,1);
    vertices_.insert(vertices_.end(),{a,b,d,b,e,d});
}
void Renderer::additive(const std::function<void()>& draw) {
    const auto first=vertices_.size();draw();
    if(vertices_.size()>first)additiveRanges_.emplace_back(first,vertices_.size());
}
void Renderer::scaled(float scale,float cx,float cy,const std::function<void()>& draw) {
    const auto first=vertices_.size();const auto previous=scaledScene_;
    // Layout uses the inverse of the eventual zoom, including on narrow phones.
    if(scale>0)scaledScene_=std::array<float,2>{cx+(scene_left()-cx)/scale,cx+(scene_right()-cx)/scale};
    try{draw();}catch(...){scaledScene_=previous;throw;}
    scaledScene_=previous;
    if(scale==1)return;
    for(auto i=first;i<vertices_.size();++i) {
        auto& v=vertices_[i];v.x=cx+(v.x-cx)*scale;v.y=cy+(v.y-cy)*scale;
    }
}
void Renderer::board_effect(float cx,float cy,float unit,const FieldTransform& motion,const std::function<void()>& draw) {
    const auto first=vertices_.size();draw();
    if(motion.x==0 && motion.y==0 && motion.rotation==0)return;
    const float angle=motion.rotation*3.14159265f/180,cosine=std::cos(angle),sine=std::sin(angle);
    // Clip primitives in board coordinates, then transform the complete group.
    // This keeps its well, border, queues, HUD and effects rigidly connected.
    for(auto i=first;i<vertices_.size();++i) {
        auto& v=vertices_[i];const float x=v.x-cx,y=v.y-cy;
        v.x=cx+x*cosine-y*sine+motion.x*unit;v.y=cy+x*sine+y*cosine+motion.y*unit;
    }
}
void Renderer::mino(float x,float y,float size,Color c,bool ghost,int style) {
    mino(Rect{x,y,size,size},c,ghost,style);
}
void Renderer::mino(Rect bounds,Color c,bool ghost,int style) {
    const float size=std::min(bounds.w,bounds.h),gap=std::min(1.f,size*.04f);
    const float w=bounds.w*(style?.65f:1.f),h=bounds.h;
    const float x=bounds.x+(bounds.w-w)/2,y=bounds.y;
    if(ghost) {
        rect(x+gap,y+gap,w-2*gap,h-2*gap,{c.r,c.g,c.b,.065f*c.a});
        outline(x+gap,y+gap,w-2*gap,h-2*gap,{c.r,c.g,c.b,.5f*c.a},std::min(1.4f,size*.09f));return;
    }
    gradient(x+gap,y+gap,w-2*gap,h-2*gap,c,{c.r*.62f,c.g*.62f,c.b*.62f,c.a});
    rect(x+gap,y+gap,w-2*gap,std::min(2.f,size*.08f),{1,1,1,.35f*c.a});
    rect(x+gap,y+gap,std::min(1.f,size*.05f),h-2*gap,{1,1,1,.14f*c.a});
    rect(x+w-gap-1,y+gap,1,h-2*gap,{0,0,0,.16f*c.a});
}
float Renderer::measure(const std::string& value,float size) {
    size=text_size(size);
    float width=0,lineWidth=0;const char* cursor=value.c_str();
    while(const auto code=SDL_StepUTF8(&cursor,nullptr)) {
        if(code==U'\n'){width=std::max(width,lineWidth);lineWidth=0;}
        else {const auto g=glyph(code,size);lineWidth+=g.advance*size/g.em;}
    }
    return std::max(width,lineWidth);
}
void Renderer::fitted(float x,float y,const std::string& value,float width,float size,Color color) {
    size=text_size(size);
    if(width<=0 || size<=0)return;
    if(measure(value,size)<=width){text(x,y,value,size,color);return;}
    std::string result;const char* cursor=value.c_str();
    const float suffix=measure("…",size);if(suffix>width)return;float used=0;
    for(;;) {
        const char* start=cursor;const auto code=SDL_StepUTF8(&cursor,nullptr);if(!code)break;
        const auto g=glyph(code,size);const float next=g.advance*size/g.em;if(used+next+suffix>width)break;
        result.append(start,cursor-start);used+=next;
    }
    text(x,y,result+"…",size,color);
}
std::vector<std::string> Renderer::wrap(const std::string& value,float width,float size) {
    size=text_size(size);
    // Break at spaces where possible, and at codepoints for CJK/long paths.
    std::vector<std::string> lines;std::string current;float used=0;
    const char* cursor=value.c_str();
    while(const auto code=SDL_StepUTF8(&cursor,nullptr)) {
        const auto length=code<0x80?1:code<0x800?2:code<0x10000?3:4;
        const std::string next(cursor-length,length);const auto g=glyph(code,size);const float advance=g.advance*size/g.em;
        if(code==U'\n'){lines.push_back(current);current.clear();used=0;continue;}
        if(used+advance>width && !current.empty()) {
            const auto space=current.find_last_of(' ');
            if(space!=std::string::npos && space>current.size()/2) {
                lines.push_back(current.substr(0,space));current=current.substr(space+1);used=measure(current,size);
            } else {lines.push_back(current);current.clear();used=0;}
        }
        if(current.empty() && code==U' ')continue;
        current+=next;used+=advance;
    }
    if(!current.empty())lines.push_back(current);
    return lines;
}
float Renderer::paragraph(float x,float y,const std::string& value,float width,float size,Color color,int maxLines) {
    size=text_size(size);
    const auto lines=wrap(value,width,size);
    for(std::size_t i=0;i<lines.size() && i<static_cast<std::size_t>(maxLines);++i)
        fitted(x,y+i*size*1.55f,lines[i]+(i+1==static_cast<std::size_t>(maxLines) && i+1<lines.size()?"…":""),width,size,color);
    return std::min<int>(maxLines,lines.size())*size*1.55f;
}
float Renderer::scroll_viewport(const Frontend::UiMetadata& m,const std::string& key,
    std::size_t selected,std::size_t count,std::size_t visible,Rect bounds,float cellSize,bool horizontal) {
    if(menuScroll_.size()>128)menuScroll_.clear();
    hitMetadata_=m;menuScrollAreas_.push_back({bounds,key});
    float movement=0;
    for(const auto& event:menuScrollEvents_)if(event.page==m.page && event.key==key)movement+=event.rows;
    const float offset=menuScroll_[key].show(selected,count,visible,m.page,movement);
    const float maximum=float(count>visible?count-visible:0),extent=horizontal?bounds.w:bounds.h;
    const float length=count?std::min(extent,std::max(24.f,extent*visible/count)):extent;
    const float position=maximum>0?offset/maximum*(extent-length):0;
    const Rect track=horizontal?Rect{bounds.x,bounds.y+bounds.h-10,bounds.w,10}:
        Rect{bounds.x+bounds.w-10,bounds.y,10,bounds.h};
    const Rect thumb=horizontal?Rect{track.x+position,track.y,length,track.h}:Rect{track.x,track.y+position,track.w,length};
    menuScrollMetrics_.push_back({bounds,key,cellSize,offset,maximum,track,thumb,horizontal});
    if(maximum>0) {
        if(horizontal){rounded(track.x,track.y+3,track.w,3,1,borderColor);rounded(thumb.x,thumb.y+3,thumb.w,3,1,accent);}
        else {rounded(track.x+3,track.y,3,track.h,1,borderColor);rounded(thumb.x+3,thumb.y,3,thumb.h,1,accent);}
    }
    return offset;
}
std::optional<UiHit> Renderer::hit(float x,float y,const Frontend::UiMetadata& current) const {
    return interaction().hit(x,y,current);
}
Renderer::Glyph Renderer::glyph(char32_t code,float size) {
    // Rasterize close to the actual screen size: shrinking a 96px CJK bitmap
    // into a 9px label loses entire strokes with ordinary bilinear sampling.
    const int fontPixels=std::clamp(static_cast<int>(std::lround(size*viewportScale_)),8,192);
    const auto key=(static_cast<std::uint64_t>(fontPixels)<<32)|static_cast<std::uint32_t>(code);
    if(const auto found=glyphs_.find(key);found!=glyphs_.end()) return found->second;
    FT_Set_Pixel_Sizes(face_,0,fontPixels);
    if(FT_Load_Char(face_,code,FT_LOAD_RENDER)) return fallback_;
    const auto& bitmap=face_->glyph->bitmap;
    const int w=bitmap.width,h=bitmap.rows;
    if(atlasX_+w+1>=atlasSize) {atlasX_=2;atlasY_+=atlasRow_+2;atlasRow_=0;}
    if(atlasY_+h+1>=atlasSize) return fallback_;
    std::vector<unsigned char> pixels(w*h);
    for(int row=0;row<h;++row) std::copy_n(bitmap.buffer+row*bitmap.pitch,w,pixels.begin()+row*w);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    if(w && h) glTexSubImage2D(GL_TEXTURE_2D,0,atlasX_,atlasY_,w,h,GL_RED,GL_UNSIGNED_BYTE,pixels.data());
    Glyph result{float(atlasX_),float(atlasY_),float(w),float(h),float(face_->glyph->bitmap_left),
        float(face_->glyph->bitmap_top),face_->glyph->advance.x/64.f,float(fontPixels)};
    atlasX_+=w+2;atlasRow_=std::max(atlasRow_,h);glyphs_[key]=result;return result;
}
void Renderer::text(float x,float y,const std::string& value,float size,Color color) {
    draw_text(x,y,value,text_size(size),color);
}
void Renderer::draw_text(float x,float y,const std::string& value,float size,Color color) {
    const float origin=x;
    const char* cursor=value.c_str();
    while(const auto code=SDL_StepUTF8(&cursor,nullptr)) {
        if(code==U'\n') {y+=size*1.5f;x=origin;continue;}
        const auto g=glyph(code,size);const float scale=size/g.em;
        quad(x+g.left*scale,y+size-g.top*scale,g.w*scale,g.h*scale,
            g.x/atlasSize,g.y/atlasSize,g.w/atlasSize,g.h/atlasSize,color);
        x+=g.advance*scale;
    }
}
float Renderer::hud_text(float right,float baseline,const std::string& value,float size,Color color,float width,float tracking,float condensed) {
    size=text_size(size);
    if(value.empty() || width<=0)return 0;
    float advance=0;const char* cursor=value.c_str();
    while(const auto code=SDL_StepUTF8(&cursor,nullptr)) {
        const auto g=glyph(code,size);advance+=g.advance*size/g.em*condensed+tracking;
    }
    advance-=tracking;
    const float fit=std::min(1.f,width/std::max(1.f,advance));
    float x=right-advance*fit;cursor=value.c_str();
    while(const auto code=SDL_StepUTF8(&cursor,nullptr)) {
        const auto g=glyph(code,size);const float scale=size/g.em*fit;
        quad(x+g.left*scale*condensed,baseline-g.top*scale,g.w*scale*condensed,g.h*scale,
            g.x/atlasSize,g.y/atlasSize,g.w/atlasSize,g.h/atlasSize,color);
        x+=(g.advance*size/g.em*condensed+tracking)*fit;
    }
    return advance*fit;
}
const BoardEvents& Renderer::events(int slot,const Presentation::BoardView& board,const SessionView& session) {
    if(eventStates_.size()>64)eventStates_.clear();
    auto& state=eventStates_[slot];
    if(session.status==SessionStatus::Countdown) {state=BoardEvents{};return state;}
    state.update(board,animationTime_,session.status==SessionStatus::Paused ||
        (session.replayPaused && session.status!=SessionStatus::Finished),
        session.replay,session.replaySpeed,board.finished || board.result!=Presentation::RoundResult::None || session.status==SessionStatus::Finished,
        session.roundRevision);
    return state;
}
const FieldEffects& Renderer::field_effects(int slot,const Presentation::BoardView& board,const SessionView& session) {
    if(fieldEffects_.size()>64)fieldEffects_.clear();
    auto& state=fieldEffects_[slot];
    if(session.status==SessionStatus::Countdown){state=FieldEffects{};return state;}
    state.update(board,animationTime_,session.status==SessionStatus::Paused ||
        (session.replayPaused && session.status!=SessionStatus::Finished),session.replay,session.replaySpeed,
        board.finished || board.result!=Presentation::RoundResult::None || session.status==SessionStatus::Finished,session.roundRevision);
    return state;
}
void Renderer::text_effect(const TextMotion& motion,float anchorX,float anchorY,const std::function<void()>& draw) {
    if(motion.opacity<=0 && motion.echoOpacity<=0)return;
    const auto first=vertices_.size();const auto bounds=clip_;
    clip_.reset();draw();clip_=bounds;const auto last=vertices_.size();
    const auto transform=[&](std::size_t begin,std::size_t end,float scale,float spread,float opacity) {
        for(auto q=begin;q+5<end;q+=6) {
            float left=vertices_[q].x,right=left;
            for(auto j=q+1;j<q+6;++j){left=std::min(left,vertices_[j].x);right=std::max(right,vertices_[j].x);}
            const float drift=((left+right)/2-anchorX)*spread;
            for(auto j=q;j<q+6;++j) {
                auto& v=vertices_[j];v.x=anchorX+(v.x-anchorX+drift)*scale;
                v.y=anchorY+(v.y-anchorY)*scale+motion.dy;v.a*=opacity;
            }
        }
    };
    if(motion.echoOpacity>0 && first<last) {
        const std::vector<Vertex> original(vertices_.begin()+first,vertices_.begin()+last);
        vertices_.insert(vertices_.end(),original.begin(),original.end());
        // The outward copy sits behind the main text and fades independently.
        transform(first,last,motion.echoScale,motion.spread,motion.echoOpacity);
        transform(last,vertices_.size(),motion.scale,motion.spread,motion.opacity);
    } else transform(first,last,motion.scale,motion.spread,motion.opacity);
    if(bounds && first<vertices_.size()) {
        // Clip after motion, including slanted glyphs and the expanding echo.
        // Interpolate UVs along clipped triangles instead of stretching ink.
        const std::vector<Vertex> moved(vertices_.begin()+first,vertices_.end());
        vertices_.resize(first);
        const auto mix=[](const Vertex& a,const Vertex& b,float t) {
            return Vertex{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t,
                a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,a.a+(b.a-a.a)*t};
        };
        for(std::size_t i=0;i+2<moved.size();i+=3) {
            const auto inside=[&](const Vertex& v) {return v.x>=bounds->x && v.x<=bounds->x+bounds->w &&
                v.y>=bounds->y && v.y<=bounds->y+bounds->h;};
            if(inside(moved[i]) && inside(moved[i+1]) && inside(moved[i+2])) {
                vertices_.insert(vertices_.end(),{moved[i],moved[i+1],moved[i+2]});continue;
            }
            std::vector<Vertex> polygon{moved[i],moved[i+1],moved[i+2]};
            for(int edge=0;edge<4 && !polygon.empty();++edge) {
                const auto distance=[&](const Vertex& v) {
                    return edge==0?v.x-bounds->x:edge==1?bounds->x+bounds->w-v.x:
                        edge==2?v.y-bounds->y:bounds->y+bounds->h-v.y;
                };
                std::vector<Vertex> clipped;auto previous=polygon.back();float before=distance(previous);
                for(const auto& current:polygon) {
                    const float after=distance(current);
                    if((after>=0)!=(before>=0))clipped.push_back(mix(previous,current,before/(before-after)));
                    if(after>=0)clipped.push_back(current);
                    previous=current;before=after;
                }
                polygon=std::move(clipped);
            }
            for(std::size_t j=1;j+1<polygon.size();++j)
                vertices_.insert(vertices_.end(),{polygon[0],polygon[j],polygon[j+1]});
        }
    }
}
void Renderer::centered_text(Rect box,const std::string& value,float size,Color color,float edge,float slant) {
    size=text_size(size);
    if(value.empty() || box.w<=2*edge || box.h<=2*edge)return;
    const auto bounds=[&](float em) {
        float pen=0,left=0,top=0,right=0,bottom=0;bool first=true;
        const char* cursor=value.c_str();
        while(const auto code=SDL_StepUTF8(&cursor,nullptr)) {
            const auto g=glyph(code,em);const float scale=em/g.em;
            if(g.w && g.h) {
                const float x=pen+g.left*scale,y=em-g.top*scale;
                left=first?x:std::min(left,x);top=first?y:std::min(top,y);
                right=std::max(right,x+g.w*scale);bottom=std::max(bottom,y+g.h*scale);first=false;
            }
            pen+=g.advance*scale;
        }
        return Rect{left,top,right-left,bottom-top};
    };
    auto ink=bounds(size);
    size*=std::min({1.f,(box.w-2*edge)/std::max(1.f,ink.w+std::abs(slant)*ink.h),
        (box.h-2*edge)/std::max(1.f,ink.h)});
    ink=bounds(size);
    const float x=box.x+(box.w-ink.w)/2-ink.x,y=box.y+(box.h-ink.h)/2-ink.y;
    const auto start=vertices_.size();
    if(edge>0)for(int i=0;i<12;++i) {
        const float angle=i*6.2831853f/12;
        draw_text(x+std::cos(angle)*edge,y+std::sin(angle)*edge,value,size,wellColor);
    }
    draw_text(x,y,value,size,color);
    if(slant)for(auto i=start;i<vertices_.size();++i)
        vertices_[i].x+=slant*(box.y+box.h/2-vertices_[i].y);
}
void Renderer::end() {
    const auto page=hitMetadata_?hitMetadata_->page:0;
    if(page!=hoverPage_){hoverBounds_.reset();hoverAmount_=0;hoverPage_=page;}
    const UiHit* hovered=nullptr;
    if(pointer_ && hitMetadata_)for(const auto& hit:hits_)
        if((*pointer_)[0]>=hit.bounds.x && (*pointer_)[0]<hit.bounds.x+hit.bounds.w &&
           (*pointer_)[1]>=hit.bounds.y && (*pointer_)[1]<hit.bounds.y+hit.bounds.h){hovered=&hit;break;}
    const float blend=1-std::exp(-20*std::clamp(animationTime_-hoverTime_,0.,.1));hoverTime_=animationTime_;
    if(hovered) {
        const auto b=hovered->bounds;
        if(!hoverBounds_ || hoverBounds_->x!=b.x || hoverBounds_->y!=b.y || hoverBounds_->w!=b.w || hoverBounds_->h!=b.h)hoverAmount_=0;
        hoverBounds_=b;hoverAmount_+=(1-hoverAmount_)*blend;
    } else hoverAmount_*=1-blend;
    if(hoverBounds_ && hoverAmount_>.005f) {
        const auto b=*hoverBounds_;const float s=std::max(1.f,1.f/viewportScale_);
        clip({});rounded(b.x,b.y,b.w,b.h,5,{accent.r,accent.g,accent.b,.075f*hoverAmount_});
        outline(b.x,b.y,b.w,b.h,{accent.r,accent.g,accent.b,.55f*hoverAmount_},s);
    }
    glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,vertices_.size()*sizeof(Vertex),vertices_.data(),GL_STREAM_DRAW);
    std::size_t first=0;
    for(const auto& range:additiveRanges_) {
        if(range.first>first)glDrawArrays(GL_TRIANGLES,static_cast<GLint>(first),static_cast<GLsizei>(range.first-first));
        glBlendFunc(GL_SRC_ALPHA,GL_ONE);
        glDrawArrays(GL_TRIANGLES,static_cast<GLint>(range.first),static_cast<GLsizei>(range.second-range.first));
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);first=range.second;
    }
    if(vertices_.size()>first)glDrawArrays(GL_TRIANGLES,static_cast<GLint>(first),static_cast<GLsizei>(vertices_.size()-first));
    if(const auto error=glGetError();error!=GL_NO_ERROR) throw std::runtime_error("OpenGL draw error: "+std::to_string(error));
}
void Renderer::surface(const Frontend::UiFrame& frame) {
    if(!frame.overlay)for(auto& entry:eventStates_)entry.second.suspend(animationTime_);
    if(!frame.overlay)for(auto& entry:fieldEffects_)entry.second.suspend(animationTime_);
    if(menu(frame.metadata))return;
    if(application_surface(frame))return;
    if(!frame.rows.empty())throw std::logic_error("UI surface is missing its graphical model: "+frame.metadata.id);
}
void Renderer::surface_in(const Frontend::UiFrame& frame,Rect pane,float zoom,float top) {
    if(pane.w<=0 || pane.h<=0)return;
    const auto first=vertices_.size(),firstHit=hits_.size(),firstScroll=scrollAreas_.size(),firstMenuScroll=menuScrollAreas_.size(),firstMetrics=menuScrollMetrics_.size();
    // Reflow into the pane's actual aspect ratio. The same uniform transform
    // applies to glyphs, cells and every interaction rectangle.
    const float scale=pane.h/800*zoom,previousWidth=uiWidth_;
    uiWidth_=pane.w/scale;
    try {surface(frame);}catch(...){uiWidth_=previousWidth;throw;}
    uiWidth_=previousWidth;
    const auto map=[&](Rect r){return Rect{pane.x+r.x*scale,pane.y+(r.y-top)*scale,r.w*scale,r.h*scale};};
    for(auto i=first;i<vertices_.size();++i) {
        vertices_[i].x=pane.x+vertices_[i].x*scale;
        vertices_[i].y=pane.y+(vertices_[i].y-top)*scale;
    }
    for(auto i=firstHit;i<hits_.size();++i)hits_[i].bounds=map(hits_[i].bounds);
    for(auto i=firstScroll;i<scrollAreas_.size();++i)scrollAreas_[i].first=map(scrollAreas_[i].first);
    for(auto i=firstMenuScroll;i<menuScrollAreas_.size();++i)menuScrollAreas_[i].first=map(menuScrollAreas_[i].first);
    for(auto i=firstMetrics;i<menuScrollMetrics_.size();++i) {
        auto& m=menuScrollMetrics_[i];m.bounds=map(m.bounds);m.track=map(m.track);m.thumb=map(m.thumb);m.rowHeight*=scale;
    }
    if(inputRect_)inputRect_=map(*inputRect_);
    if(editorField_)editorField_->bounds=map(editorField_->bounds);
    if(zoom!=1 || top!=0) {
        // Enlarged pages use their former top margin. Clip their complete
        // geometry after reflow so backgrounds cannot cover the header/rail.
        const std::vector<Vertex> drawn(vertices_.begin()+first,vertices_.end());vertices_.resize(first);
        const auto mix=[](const Vertex& a,const Vertex& b,float t) {
            return Vertex{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t,
                a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,a.a+(b.a-a.a)*t};
        };
        for(std::size_t i=0;i+2<drawn.size();i+=3) {
            std::vector<Vertex> polygon{drawn[i],drawn[i+1],drawn[i+2]};
            for(int edge=0;edge<4 && !polygon.empty();++edge) {
                const auto distance=[&](const Vertex& v) {return edge==0?v.x-pane.x:edge==1?pane.x+pane.w-v.x:
                    edge==2?v.y-pane.y:pane.y+pane.h-v.y;};
                std::vector<Vertex> clipped;auto previous=polygon.back();float before=distance(previous);
                for(const auto& current:polygon) {
                    const float after=distance(current);
                    if((after>=0)!=(before>=0))clipped.push_back(mix(previous,current,before/(before-after)));
                    if(after>=0)clipped.push_back(current);previous=current;before=after;
                }
                polygon=std::move(clipped);
            }
            for(std::size_t j=1;j+1<polygon.size();++j)vertices_.insert(vertices_.end(),{polygon[0],polygon[j],polygon[j+1]});
        }
    }
}
void Renderer::screenshot(const std::string& path,int w,int h) {
    std::vector<unsigned char> pixels(w*h*4),flipped(pixels.size());
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    for(int y=0;y<h;++y) std::copy_n(pixels.data()+(h-y-1)*w*4,w*4,flipped.data()+y*w*4);
    auto* surface=SDL_CreateSurfaceFrom(w,h,SDL_PIXELFORMAT_RGBA32,flipped.data(),w*4);
    if(!surface) throw std::runtime_error(SDL_GetError());
    const bool ok=SDL_SaveBMP(surface,path.c_str());SDL_DestroySurface(surface);
    if(!ok) throw std::runtime_error(SDL_GetError());
}
Color cell_color(int id,bool obscured) {
    if(obscured) return {.68f,.68f,.68f};
    static constexpr std::array<Color,10> colors{{
        rgb(0x36EBA8),rgb(0x5F4EB3),rgb(0xC57544),rgb(0xC1A841),
        rgb(0x8BB93D),rgb(0xB04EA6),rgb(0xCA464D),
        rgb(0x4E4E4E),rgb(0x272727),rgb(0x4D003B)}};
    return colors[std::clamp(id,0,9)];
}
}
