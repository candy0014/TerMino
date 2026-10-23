#include "tetr/frontend/virtual_controls.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace Frontend::Touch {
namespace {
constexpr double pi=3.14159265358979323846;
double cross(Point a,Point b,Point c) {return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
bool on_segment(Point a,Point b,Point p) {
    return std::abs(cross(a,b,p))<1e-9 && p.x>=std::min(a.x,b.x)-1e-9 &&
        p.x<=std::max(a.x,b.x)+1e-9 && p.y>=std::min(a.y,b.y)-1e-9 && p.y<=std::max(a.y,b.y)+1e-9;
}
bool intersects(Point a,Point b,Point c,Point d) {
    return (cross(a,b,c)*cross(a,b,d)<0 && cross(c,d,a)*cross(c,d,b)<0) ||
        on_segment(a,b,c) || on_segment(a,b,d) || on_segment(c,d,a) || on_segment(c,d,b);
}
bool unit(double v) {return std::isfinite(v) && v>=0 && v<=1;}
bool identifier(const std::string& s) {
    const auto alnum=[](unsigned char c){return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');};
    return !s.empty() && s.size()<=64 && alnum(s.front()) &&
        std::all_of(s.begin(),s.end(),[&](unsigned char c){return alnum(c) || c=='_' || c=='-';});
}
Point transform(Point p,const Button& b,const Area& a) {
    const double u=std::min(a.width,a.height),angle=b.rotation*pi/180;
    const double x=(p.x-.5)*b.width*u,y=(p.y-.5)*b.height*u;
    return {a.x+b.center.x*a.width+x*std::cos(angle)-y*std::sin(angle),
        a.y+b.center.y*a.height+x*std::sin(angle)+y*std::cos(angle)};
}
}
Configuration defaults() {
    Configuration c;Layout l;
    const std::array<Point,11> centers={Point{.69,.72},{.86,.72},{.775,.885},{.775,.555},{.23,.555},
        {.145,.72},{.23,.885},{.315,.72},{.145,.38},{.86,.38},{.315,.38}};
    for(std::size_t i=0;i<actions.size();++i) {
        Button b;b.id=actions[i];b.action=actions[i];b.center=centers[i];
        b.shape=Shape::Ellipse;b.width=b.height=.21;b.opacity=.4;
        l.buttons.push_back(b);
    }
    c.layouts.push_back(l);return c;
}
bool valid(const Configuration& c,std::string* error) {
    const auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(c.version!=1 || c.layouts.empty() || c.layouts.size()>32)return fail("Unsupported or empty layout document");
    bool active=false;std::set<std::string> layouts;
    for(const auto& l:c.layouts) {
        if(!identifier(l.id) || !layouts.insert(l.id).second || l.name.empty() || l.name.size()>512 || l.buttons.size()>256)
            return fail("Invalid or duplicate layout");
        active|=l.id==c.active;std::set<std::string> ids;
        for(const auto& b:l.buttons) {
            if(!identifier(b.id) || !ids.insert(b.id).second || b.player<0 || b.player>1 ||
                std::find(actions.begin(),actions.end(),b.action)==actions.end())return fail("Invalid button identity or action");
            if(b.shape!=Shape::Rectangle && b.shape!=Shape::RoundedRectangle && b.shape!=Shape::Ellipse && b.shape!=Shape::Polygon)
                return fail("Unknown button shape");
            if(!unit(b.center.x) || !unit(b.center.y) || !unit(b.opacity) || !std::isfinite(b.width) || !std::isfinite(b.height) ||
                b.width<=0 || b.height<=0 || b.width>8 || b.height>8 || !std::isfinite(b.rotation) ||
                b.rotation< -180 || b.rotation>180 || !unit(b.cornerRadius) || b.cornerRadius>.5)return fail("Invalid button geometry");
            if(b.shape==Shape::Polygon) {
                const auto n=b.vertices.size();if(n<3 || n>64)return fail("Polygon needs 3..64 vertices");
                double area=0;
                for(std::size_t i=0;i<n;++i) {
                    auto a=b.vertices[i],d=b.vertices[(i+1)%n];
                    if(!unit(a.x) || !unit(a.y) || (std::abs(a.x-d.x)<1e-9 && std::abs(a.y-d.y)<1e-9))return fail("Invalid polygon vertex");
                    area+=a.x*d.y-a.y*d.x;
                    for(std::size_t j=i+1;j<n;++j)if(j!=i+1 && !(i==0 && j+1==n) &&
                        intersects(a,d,b.vertices[j],b.vertices[(j+1)%n]))return fail("Polygon edges must not intersect");
                }
                if(std::abs(area)<1e-8)return fail("Polygon has zero area");
            }
        }
    }
    return active || fail("Active layout not found");
}
const Layout& active_layout(const Configuration& c) {
    for(const auto& l:c.layouts)if(l.id==c.active)return l;
    throw std::runtime_error("Active touch layout not found");
}
std::vector<Point> outline(const Button& b,const Area& a,int segments) {
    segments=std::clamp(segments,4,512);
    std::vector<Point> points;
    if(b.shape==Shape::Polygon)points=b.vertices;
    else if(b.shape==Shape::Ellipse)for(int i=0;i<segments;++i) {
        const double t=i*2*pi/segments;points.push_back({.5+.5*std::cos(t),.5+.5*std::sin(t)});
    } else if(b.shape==Shape::RoundedRectangle && b.cornerRadius>0) {
        const double r=b.cornerRadius*std::min(b.width,b.height),rx=r/b.width,ry=r/b.height;
        for(int q=0;q<4;++q)for(int i=0;i<=segments/4;++i) {
            const double t=(q*90+i*90./(segments/4))*pi/180;
            const double x=q==0 || q==3?1-rx:rx,y=q<2?1-ry:ry;
            points.push_back({x+rx*std::cos(t),y+ry*std::sin(t)});
        }
    } else points={{0,0},{1,0},{1,1},{0,1}};
    for(auto& p:points)p=transform(p,b,a);return points;
}
bool contains(const Button& b,const Area& a,Point p) {
    if(a.width<=0 || a.height<=0 || p.x<a.x || p.y<a.y || p.x>a.x+a.width || p.y>a.y+a.height)return false;
    const double angle=-b.rotation*pi/180,u=std::min(a.width,a.height);
    const double dx=p.x-a.x-b.center.x*a.width,dy=p.y-a.y-b.center.y*a.height;
    const double x=(dx*std::cos(angle)-dy*std::sin(angle))/(b.width*u)+.5,
        y=(dx*std::sin(angle)+dy*std::cos(angle))/(b.height*u)+.5;
    if(x<0 || y<0 || x>1 || y>1)return false;
    if(b.shape==Shape::Ellipse)return std::pow(2*x-1,2)+std::pow(2*y-1,2)<=1+1e-9;
    if(b.shape==Shape::RoundedRectangle) {
        const double r=b.cornerRadius*std::min(b.width,b.height),rx=r/b.width,ry=r/b.height;
        if(rx<=0 || ry<=0)return true;
        const double cx=std::clamp(x,rx,1-rx),cy=std::clamp(y,ry,1-ry);
        return std::pow((x-cx)/rx,2)+std::pow((y-cy)/ry,2)<=1+1e-9;
    }
    if(b.shape!=Shape::Polygon)return true;
    bool inside=false;const Point local{x,y};const auto n=b.vertices.size();
    for(std::size_t i=0,j=n-1;i<n;j=i++) {
        const auto a=b.vertices[i],d=b.vertices[j];if(on_segment(a,d,local))return true;
        if((a.y>y)!=(d.y>y) && x<(d.x-a.x)*(y-a.y)/(d.y-a.y)+a.x)inside=!inside;
    }
    return inside;
}
std::optional<std::size_t> hit(const Layout& l,const Area& a,Point p,bool all) {
    std::optional<std::size_t> found;
    for(std::size_t i=0;i<l.buttons.size();++i)if((all || (l.buttons[i].enabled && l.buttons[i].available)) && contains(l.buttons[i],a,p) &&
        (!found || l.buttons[i].z>=l.buttons[*found].z))found=i;
    return found;
}
std::vector<Transition> Router::down(Finger f,const Layout& l,const Area& a,Point p) {
    if(fingers_.count(f))return {};
    const auto i=hit(l,a,p);if(!i)return {};
    const auto& b=l.buttons[*i];auto key=std::make_pair(b.player,b.action);fingers_[f]=key;
    if(++counts_[key]==1)return {{b.action,b.player,true}};return {};
}
std::vector<Transition> Router::up(Finger f) {
    const auto it=fingers_.find(f);if(it==fingers_.end())return {};
    auto key=it->second;fingers_.erase(it);
    if(--counts_[key]==0){counts_.erase(key);return {{key.second,key.first,false}};}return {};
}
std::vector<Transition> Router::cancel_unavailable(const Layout& layout) {
    std::vector<Finger> release;
    for(const auto& [finger,key]:fingers_)
        if(std::none_of(layout.buttons.begin(),layout.buttons.end(),[&](const auto& b) {
            return b.enabled && b.available && b.player==key.first && b.action==key.second;
        }))release.push_back(finger);
    std::vector<Transition> result;
    for(const auto finger:release)for(auto change:up(finger))result.push_back(std::move(change));
    return result;
}
std::vector<Transition> Router::cancel() {
    std::vector<Transition> result;for(const auto& pair:counts_)result.push_back({pair.first.second,pair.first.first,false});
    fingers_.clear();counts_.clear();return result;
}
}
