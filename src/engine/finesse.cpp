#include "tetr/engine/finesse.h"
#include <algorithm>
#include <array>
#include <queue>
#include <set>
#include <tuple>
namespace Engine {
int minimal_input_count(const Runtime::Playfield& field,const Piece::Definition& piece,Piece::Pose landing,const Runtime::MovementRestrictions& restricted) {
    const auto cells=[&](Piece::Pose pose){std::vector<std::array<int,2>> result;for(const auto& d:piece.cells(pose.rotation))result.push_back({pose.position.row+d[0],pose.position.col+d[1]});std::sort(result.begin(),result.end());return result;};
    const auto target=cells(landing);const auto start=Piece::spawn_pose(field.width(),piece.type());
    if(!piece.can_place(start,field))return -1;
    std::queue<std::pair<Piece::Pose,int>> queue;std::set<std::tuple<int,int,int>> seen;
    const auto add=[&](Piece::Pose pose,int cost){if(piece.can_place(pose,field) && seen.emplace(pose.position.row,pose.position.col,pose.rotation.index()).second)queue.push({pose,cost});};
    add(start,0);
    while(!queue.empty()) {
        const auto [pose,cost]=queue.front();queue.pop();
        auto floor=pose;while(piece.can_place(floor.translated(1,0),field))++floor.position.row;
        if(!restricted.disableHardDrop && cells(floor)==target)return cost+1;
        for(int direction:{-1,1})if(restricted.allows_horizontal(direction)) {
            add(pose.translated(0,direction),cost+1);
            auto wall=pose;while(piece.can_place(wall.translated(0,direction),field))wall.position.col+=direction;
            add(wall,cost+1);
        }
        for(int delta:{-1,1,2})if(restricted.allows_rotation(delta)) {
            auto rotated=pose;Piece::KickIndex kick;
            if(piece.try_rotate(rotated,pose.rotation.rotated(delta),field,kick))add(rotated,cost+1);
        }
        // One held soft-drop press may be released at any reachable row.
        if(!restricted.disableSoftDrop)for(auto down=pose.translated(1,0);piece.can_place(down,field);++down.position.row)add(down,cost+1);
    }
    return -1;
}
}
