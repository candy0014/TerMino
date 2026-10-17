#include "renderer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace Graphical {
namespace {
constexpr int atlasSize=2048;
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
        FT_Set_Pixel_Sizes(face_,0,48);
        const char* vertex=R"(#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
uniform vec2 viewport;
out vec2 texcoord; out vec4 tint;
void main(){gl_Position=vec4(position.x/viewport.x*2.0-1.0,1.0-position.y/viewport.y*2.0,0,1);texcoord=uv;tint=color;})";
        const char* fragment=R"(#version 330 core
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
        std::vector<unsigned char> empty(atlasSize*atlasSize,0);empty[0]=255;
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8,atlasSize,atlasSize,0,GL_RED,GL_UNSIGNED_BYTE,empty.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glyph(U'?');
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
void Renderer::begin(int width,int height) {
    vertices_.clear();
    const float scale=std::min(width/1200.f,height/800.f);
    const int w=static_cast<int>(1200*scale),h=static_cast<int>(800*scale);
    glViewport((width-w)/2,(height-h)/2,w,h);
    glClearColor(.018f,.03f,.055f,1);glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program_);glUniform2f(glGetUniformLocation(program_,"viewport"),1200,800);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,atlas_);
    glUniform1i(glGetUniformLocation(program_,"atlas"),0);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
void Renderer::quad(float x,float y,float w,float h,float u,float v,float uw,float vh,Color c) {
    const Vertex a{x,y,u,v,c.r,c.g,c.b,c.a},b{x+w,y,u+uw,v,c.r,c.g,c.b,c.a},
        d{x,y+h,u,v+vh,c.r,c.g,c.b,c.a},e{x+w,y+h,u+uw,v+vh,c.r,c.g,c.b,c.a};
    vertices_.insert(vertices_.end(),{a,b,d,b,e,d});
}
void Renderer::rect(float x,float y,float w,float h,Color c) {
    quad(x,y,w,h,.5f/atlasSize,.5f/atlasSize,0,0,c);
}
void Renderer::outline(float x,float y,float w,float h,Color c,float t) {
    rect(x,y,w,t,c);rect(x,y+h-t,w,t,c);rect(x,y,t,h,c);rect(x+w-t,y,t,h,c);
}
Renderer::Glyph Renderer::glyph(char32_t code) {
    if(const auto found=glyphs_.find(code);found!=glyphs_.end()) return found->second;
    if(FT_Load_Char(face_,code,FT_LOAD_RENDER)) return {};
    const auto& bitmap=face_->glyph->bitmap;
    const int w=bitmap.width,h=bitmap.rows;
    if(atlasX_+w+1>=atlasSize) {atlasX_=2;atlasY_+=atlasRow_+2;atlasRow_=0;}
    if(atlasY_+h+1>=atlasSize) return glyph(U'?');
    std::vector<unsigned char> pixels(w*h);
    for(int row=0;row<h;++row) std::copy_n(bitmap.buffer+row*bitmap.pitch,w,pixels.begin()+row*w);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    if(w && h) glTexSubImage2D(GL_TEXTURE_2D,0,atlasX_,atlasY_,w,h,GL_RED,GL_UNSIGNED_BYTE,pixels.data());
    Glyph result{float(atlasX_),float(atlasY_),float(w),float(h),float(face_->glyph->bitmap_left),
        float(face_->glyph->bitmap_top),face_->glyph->advance.x/64.f};
    atlasX_+=w+2;atlasRow_=std::max(atlasRow_,h);glyphs_[code]=result;return result;
}
void Renderer::text(float x,float y,const std::string& value,float size,Color color) {
    const float scale=size/48,origin=x;
    const char* cursor=value.c_str();
    while(const auto code=SDL_StepUTF8(&cursor,nullptr)) {
        if(code==U'\n') {y+=size*1.5f;x=origin;continue;}
        const auto g=glyph(code);
        quad(x+g.left*scale,y+size-g.top*scale,g.w*scale,g.h*scale,
            g.x/atlasSize,g.y/atlasSize,g.w/atlasSize,g.h/atlasSize,color);
        x+=g.advance*scale;
    }
}
void Renderer::end() {
    glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,vertices_.size()*sizeof(Vertex),vertices_.data(),GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices_.size()));
    if(const auto error=glGetError();error!=GL_NO_ERROR) throw std::runtime_error("OpenGL draw error: "+std::to_string(error));
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
    if(obscured) return {.62f,.75f,.87f};
    static constexpr std::array<Color,10> colors{{
        {.24f,.84f,.96f},{.35f,.49f,.99f},{1,.59f,.22f},{.98f,.83f,.25f},
        {.4f,.87f,.57f},{.76f,.43f,.98f},{.98f,.36f,.5f},
        {.47f,.57f,.68f},{.25f,.32f,.43f},{.82f,.84f,.91f}}};
    return colors[std::clamp(id,0,9)];
}
}
