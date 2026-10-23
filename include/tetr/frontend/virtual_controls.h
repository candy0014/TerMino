#ifndef TETR_FRONTEND_VIRTUAL_CONTROLS_H
#define TETR_FRONTEND_VIRTUAL_CONTROLS_H
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Frontend::Touch {
inline constexpr std::array<const char*,11> actions={"MoveLeft","MoveRight","SoftDrop","HardDrop",
    "RotateClockwise","RotateCounterclockwise","Rotate180","Hold","Pause","Retry","Undo"};
struct Point {double x=0,y=0;};
struct Area {double x=0,y=0,width=1200,height=800;};
enum class Shape {Rectangle,RoundedRectangle,Ellipse,Polygon};
struct Button {
    std::string id,action;
    int player=0;
    Point center{.5,.5};
    double width=.15,height=.15,rotation=0,opacity=.55,cornerRadius=.2;
    Shape shape=Shape::RoundedRectangle;
    std::vector<Point> vertices;
    bool enabled=true;
    int z=0;
    bool available=true; // Transient session state; never saved in a layout.
};
struct Layout {std::string id="landscape-default",name="Landscape";std::vector<Button> buttons;};
struct Configuration {int version=1;std::string active="landscape-default";std::vector<Layout> layouts;};
Configuration defaults();
bool valid(const Configuration& value,std::string* error=nullptr);
const Layout& active_layout(const Configuration& value);
std::vector<Point> outline(const Button& button,const Area& area,int segments=48);
bool contains(const Button& button,const Area& area,Point point);
std::optional<std::size_t> hit(const Layout& layout,const Area& area,Point point,bool includeDisabled=false);
struct Transition {std::string action;int player=0;bool down=false;};
// Each finger captures one button until release. Counts are per action/player,
// so releasing one of two fingers never releases the other finger's action.
class Router {
public:
    using Finger=std::pair<std::int64_t,std::int64_t>;
    std::vector<Transition> down(Finger finger,const Layout& layout,const Area& area,Point point);
    std::vector<Transition> up(Finger finger);
    std::vector<Transition> cancel();
    std::vector<Transition> cancel_unavailable(const Layout& layout);
private:
    std::map<Finger,std::pair<int,std::string>> fingers_;
    std::map<std::pair<int,std::string>,int> counts_;
};
}
#endif
