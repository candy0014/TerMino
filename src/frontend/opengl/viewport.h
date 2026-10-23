#ifndef TETR_OPENGL_VIEWPORT_H
#define TETR_OPENGL_VIEWPORT_H
#include <algorithm>
namespace Graphical {
// The same design-space transform is used for GPU pixels and logical IME
// coordinates. SDL supplies their different sizes on high-DPI displays.
struct Viewport {
    int x=0,y=0,width=0,height=0;
    float scale=0;
    // Mobile fills the complete height and reflows pages into the resulting
    // logical width. Both axes still use one scale, including touch and IME.
    static Viewport fit(int width,int height,bool fullWindow=false) {
        if(width<=0 || height<=0)return {};
        const float scale=fullWindow?height/800.f:std::min(width/1200.f,height/800.f);
        const int w=static_cast<int>(1200*scale),h=static_cast<int>(800*scale);
        return {(width-w)/2,(height-h)/2,w,h,scale};
    }
    int screen_x(float designX) const {return x+static_cast<int>(designX*scale);}
    int screen_y(float designY) const {return y+static_cast<int>(designY*scale);}
};
}
#endif
