#include "tetr/frontend/virtual_controls.h"
#include "tetr/config/virtual_controls.h"
#include "tetr/input/action.h"
#include "../third_party/fast_float/fast_float.h"
#include "tetr/util/text_codec.h"
#include <cassert>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>

namespace TetrTests::virtual_controls_test {
namespace {
using namespace Frontend::Touch;
std::string contents(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};
}
void geometry() {
    auto c=defaults();assert(valid(c) && active_layout(c).buttons.size()==11);
    assert(active_layout(c).buttons.back().action=="Undo");
    Area area{20,30,1200,800};Button b=c.layouts[0].buttons[0];b.center={.5,.5};b.width=.4;b.height=.2;
    b.shape=Shape::Rectangle;
    assert(contains(b,area,{620,430}) && contains(b,area,{770,500}));
    assert(!contains(b,area,{781,430}));
    b.rotation=90;assert(contains(b,area,{620,580}) && !contains(b,area,{770,430}));
    b.rotation=0;b.shape=Shape::Ellipse;
    assert(!contains(b,area,{770,500}) && contains(b,area,{620,500}));
    b.shape=Shape::RoundedRectangle;b.cornerRadius=.5;
    assert(!contains(b,area,{779,509}) && contains(b,area,{770,430}));
    for(auto p:outline(b,area,0))assert(std::isfinite(p.x) && std::isfinite(p.y));
    b.shape=Shape::Polygon;b.vertices={{0,0},{1,0},{1,.4},{.4,.4},{.4,1},{0,1}};
    c.layouts[0].buttons={b};assert(valid(c));
    assert(contains(b,area,{510,480}) && !contains(b,area,{700,480}));
    c.layouts[0].buttons[0].vertices={{0,0},{1,1},{0,1},{1,0}};assert(!valid(c));
    c.layouts[0].buttons[0].vertices={{0,0},{.5,0},{1,0}};assert(!valid(c));
    c=defaults();c.layouts[0].buttons[0].shape=Shape(99);assert(!valid(c));
    c=defaults();c.layouts[0].buttons[0].id="../bad";assert(!valid(c));
    c=defaults();c.layouts[0].buttons[0].width=std::numeric_limits<double>::infinity();assert(!valid(c));
    c=defaults();c.layouts[0].buttons[0].opacity=-.1;assert(!valid(c));
    c=defaults();c.layouts[0].buttons.push_back(c.layouts[0].buttons[0]);assert(!valid(c));
    c=defaults();c.active="missing";assert(!valid(c));
    Layout l;b.shape=Shape::Rectangle;b.enabled=true;b.opacity=0;b.id="one";l.buttons={b};
    auto top=b;top.id="two";top.z=1;l.buttons.push_back(top);
    assert(hit(l,area,{620,430})==1);l.buttons[1].enabled=false;
    assert(hit(l,area,{620,430})==0 && hit(l,area,{620,430},true)==1);
    assert(!hit(l,area,{-1,430}));
}
void fingers() {
    Area a;Layout l;Button b;b.id="left";b.action="MoveLeft";b.center={.4,.5};b.shape=Shape::Rectangle;
    l.buttons={b};b.id="right";b.action="RotateClockwise";b.center={.7,.5};l.buttons.push_back(b);
    b.id="p2";b.action="MoveLeft";b.player=1;b.center={.5,.8};l.buttons.push_back(b);
    Router r;
    auto t=r.down({1,1},l,a,{480,400});assert(t.size()==1 && t[0].down && t[0].player==0);
    assert(r.down({1,1},l,a,{480,400}).empty());
    assert(r.down({1,2},l,a,{480,400}).empty());
    t=r.down({2,1},l,a,{840,400});assert(t.size()==1 && t[0].action=="RotateClockwise");
    t=r.down({2,2},l,a,{600,640});assert(t.size()==1 && t[0].player==1);
    assert(r.up({1,1}).empty());
    t=r.up({1,2});assert(t.size()==1 && !t[0].down && t[0].action=="MoveLeft");
    t=r.cancel();assert(t.size()==2 && !t[0].down && !t[1].down);
    assert(r.up({2,1}).empty() && r.cancel().empty());
    r.down({3,1},l,a,{480,400});r.down({3,2},l,a,{480,400});
    l.buttons[0].available=false;
    t=r.cancel_unavailable(l);assert(t.size()==1 && !t[0].down && t[0].action=="MoveLeft");
    assert(r.up({3,1}).empty() && r.up({3,2}).empty());
    assert(!hit(l,a,{480,400}) && hit(l,a,{480,400},true)==0);
    assert(r.down({3,3},l,a,{480,400}).empty());
    l.buttons[0].available=true;assert(r.down({3,3},l,a,{480,400}).size()==1);r.cancel();
    auto keys=Input::BindingSet::from_key_codes({});keys.add_virtual_actions(1);
    for(std::size_t i=0;i<Input::kBindableActionCount;++i) {
        assert(keys.action_for(Input::virtual_action_key(i,1))==Input::kBindableActions[i]);
        assert(keys.action_for(Input::virtual_action_key(i,0))==Input::Action::Unknown);
    }
}
void persistence() {
    const auto dir=std::filesystem::temp_directory_path()/std::filesystem::u8path("termino-touch-测试-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);const auto file=dir/"config.json";
    const std::string other="{\"version\":1,\"graphics\":{\"theme\":\"unchanged\"},\"players\":{\"one\":{\"name\":\"TEST\"}}}";
    std::ofstream(file)<<other;
    auto c=UserConfig::load_virtual_controls(file);c.layouts[0].buttons[0].opacity=0;
    auto b=c.layouts[0].buttons[0];b.id="polygon";b.shape=Shape::Polygon;b.player=1;
    b.vertices={{0,0},{1,0},{.4,.4},{0,1}};b.width=2.4;b.rotation=-57.5;c.layouts[0].buttons.push_back(b);
    assert(UserConfig::save_virtual_controls(c,file));
    const auto saved=contents(file);assert(saved.find("unchanged")!=std::string::npos && saved.find("TEST")!=std::string::npos);
    auto round=UserConfig::load_virtual_controls(file);const auto& copy=round.layouts[0].buttons.back();
    assert(copy.vertices.size()==4 && copy.rotation==-57.5 && copy.player==1 && copy.width==2.4);
    assert(round.layouts[0].buttons[0].opacity==0);
    for(const auto damage:{0,1,2,3}) {
        auto text=saved;
        if(damage==0){auto p=text.find("\"virtual_controls\": {");assert(p!=std::string::npos);text.insert(p+21,"\"future_feature\":true,");}
        if(damage==1){auto p=text.find("\"orientation\": \"landscape\"");assert(p!=std::string::npos);text.replace(p,26,"\"orientation\": \"portrait\"");}
        if(damage==2){auto p=text.find("\"player\": 0");assert(p!=std::string::npos);text.replace(p,11,"\"player\": 0.5");}
        if(damage==3)text="{broken";
        std::ofstream(file,std::ios::trunc)<<text;
        bool threw=false;try{UserConfig::load_virtual_controls(file);}catch(const std::exception&){threw=true;}
        assert(threw && !UserConfig::save_virtual_controls(c,file) && contents(file)==text);
    }
    std::filesystem::remove_all(dir);
}
void decimals() {
    const auto compare=[](const std::string& s) {
        double x=42,y=42;auto a=std::from_chars(s.data(),s.data()+s.size(),x,std::chars_format::general);
        auto b=fast_float::from_chars(s.data(),s.data()+s.size(),y,fast_float::chars_format::general);
        assert(a.ec==b.ec && a.ptr-s.data()==b.ptr-s.data());
        if(a.ec==std::errc{} && std::isfinite(x))assert(std::memcmp(&x,&y,sizeof(x))==0);
        const auto actual=Util::Text::parse_finite_real(s);
        assert(bool(actual)==(a.ec==std::errc{} && a.ptr==s.data()+s.size() && std::isfinite(x) && !s.empty()));
    };
    for(const auto s:{"","+1","-0","1e-4000","1e4000","1.7976931348623157e308","4.9406564584124654e-324",
        "2.2250738585072014e-308","0.10000000000000001","1,5"," 1","1 ","nan","inf","0x1.2","-3.25e+9"})compare(s);
    std::mt19937_64 random(87314);char text[64];
    for(int i=0;i<20000;++i){auto bits=random();double value;std::memcpy(&value,&bits,sizeof(value));
        if(!std::isfinite(value))continue;auto end=std::to_chars(text,text+sizeof(text),value);assert(end.ec==std::errc{});
        compare(std::string(text,end.ptr));}
}
}
int run(int,char**) {geometry();fingers();persistence();decimals();return 0;}
}
