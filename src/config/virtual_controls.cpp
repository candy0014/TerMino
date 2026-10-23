#include "tetr/config/virtual_controls.h"
#include "config_store.h"
#include <algorithm>
#include <stdexcept>
#include <climits>

namespace UserConfig {
namespace {
using namespace Frontend::Touch;using Json=Storage::Json;
void properties(const Json& object,std::initializer_list<const char*> names) {
    if(!object.is_object())throw std::runtime_error("Expected virtual controls object");
    for(auto it=object.begin();it!=object.end();++it)
        if(std::find(names.begin(),names.end(),it.key())==names.end())
            throw std::runtime_error("Unknown virtual controls property; file preserved");
}
Point point(const Json& object) {
    properties(object,{"x","y"});return {object.at("x").get<double>(),object.at("y").get<double>()};
}
int integer(const Json& value) {
    if(!value.is_number_integer() || value<INT_MIN || value>INT_MAX)throw std::runtime_error("Invalid layout integer");
    return value.get<int>();
}
const char* shape_name(Shape s) {
    switch(s){case Shape::Rectangle:return "rectangle";case Shape::RoundedRectangle:return "rounded_rectangle";
        case Shape::Ellipse:return "ellipse";case Shape::Polygon:return "polygon";}return "";
}
Json encode(const Configuration& c) {
    Json data={{"version",c.version},{"orientation","landscape"},{"active_layout",c.active},{"layouts",Json::array()}};
    for(const auto& l:c.layouts) {
        Json layout={{"id",l.id},{"name",l.name},{"buttons",Json::array()}};
        for(const auto& b:l.buttons) {
            Json shape={{"kind",shape_name(b.shape)}};
            if(b.shape==Shape::RoundedRectangle)shape["corner_radius"]=b.cornerRadius;
            if(b.shape==Shape::Polygon){shape["vertices"]=Json::array();for(auto p:b.vertices)shape["vertices"].push_back({{"x",p.x},{"y",p.y}});}
            layout["buttons"].push_back({{"id",b.id},{"action",b.action},{"player",b.player},{"center",{{"x",b.center.x},{"y",b.center.y}}},
                {"size",{{"width",b.width},{"height",b.height}}},{"shape",shape},{"rotation_degrees",b.rotation},
                {"opacity",b.opacity},{"enabled",b.enabled},{"z_order",b.z}});
        }
        data["layouts"].push_back(layout);
    }
    return data;
}
Configuration decode(const Json& data) {
    properties(data,{"version","orientation","active_layout","layouts"});
    if(data.at("orientation")!="landscape" || !data.at("layouts").is_array())throw std::runtime_error("Unsupported virtual controls");
    Configuration c;c.version=integer(data.at("version"));c.active=data.at("active_layout").get<std::string>();
    if(data.at("layouts").size()>32)throw std::runtime_error("Too many touch layouts");
    for(const auto& layout:data.at("layouts")) {
        properties(layout,{"id","name","buttons"});
        Layout l;l.id=layout.at("id").get<std::string>();l.name=layout.at("name").get<std::string>();
        if(!layout.at("buttons").is_array() || layout.at("buttons").size()>256)throw std::runtime_error("Invalid virtual buttons");
        for(const auto& b:layout.at("buttons")) {
            properties(b,{"id","action","player","center","size","shape","rotation_degrees","opacity","enabled","z_order"});
            Button v;v.id=b.at("id").get<std::string>();v.action=b.at("action").get<std::string>();v.player=integer(b.at("player"));
            v.center=point(b.at("center"));properties(b.at("size"),{"width","height"});
            v.width=b.at("size").at("width").get<double>();v.height=b.at("size").at("height").get<double>();
            v.rotation=b.at("rotation_degrees").get<double>();v.opacity=b.at("opacity").get<double>();
            v.enabled=b.at("enabled").get<bool>();v.z=integer(b.at("z_order"));
            const auto& shape=b.at("shape");const auto kind=shape.at("kind").get<std::string>();
            if(kind=="rounded_rectangle")properties(shape,{"kind","corner_radius"});
            else if(kind=="polygon")properties(shape,{"kind","vertices"});
            else properties(shape,{"kind"});
            if(kind=="rectangle")v.shape=Shape::Rectangle;
            else if(kind=="rounded_rectangle"){v.shape=Shape::RoundedRectangle;v.cornerRadius=shape.at("corner_radius").get<double>();}
            else if(kind=="ellipse")v.shape=Shape::Ellipse;
            else if(kind=="polygon") {
                v.shape=Shape::Polygon;const auto& vertices=shape.at("vertices");
                if(!vertices.is_array() || vertices.size()<3 || vertices.size()>64)throw std::runtime_error("Invalid polygon vertices");
                for(const auto& p:vertices)v.vertices.push_back(point(p));
            }
            else throw std::runtime_error("Unknown virtual button shape");
            l.buttons.push_back(v);
        }
        c.layouts.push_back(l);
    }
    std::string error;if(!valid(c,&error))throw std::runtime_error(error);return c;
}
}
Frontend::Touch::Configuration load_virtual_controls(const std::filesystem::path& file) {
    Storage::Json document;if(!Storage::read(file,document))throw std::runtime_error("Could not read config; file preserved");
    if(!document.contains("virtual_controls"))return Frontend::Touch::defaults();
    return decode(document.at("virtual_controls"));
}
bool save_virtual_controls(const Frontend::Touch::Configuration& value,const std::filesystem::path& file) {
    if(!Frontend::Touch::valid(value))return false;
    try{return Storage::update(file,[&](auto& data){
        if(data.contains("virtual_controls"))decode(data.at("virtual_controls"));
        data["virtual_controls"]=encode(value);return true;
    });}catch(const std::exception&){return false;}
}
}
