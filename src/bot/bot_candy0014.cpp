#include "tetr/bot/bot_candy0014.h"
#include "tetr/bot/quick_play_search.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "bot_piece_access.h"
#include <vector>

namespace Bot_candy0014{

double terrain(const Bot::Environment& environment,const Runtime::Playfield& mp) {
	double score=0;
	const int mapHeightU=std::min(Runtime::kHiddenRowCapacity,8);
	int h[105],max_hole=0,cntstair[4]={0,0,0,0},max_h=0,min_h=environment.fieldHeight+mapHeightU;
	for(int i=0;i<environment.fieldWidth;i++){
		h[i]=0;
		double now=2.6,sub=1;
		for(int j=-mapHeightU;j<environment.fieldHeight;j++){
			if(mp.cell(j,i).occupied()&&h[i]==0) h[i]=environment.fieldHeight-j;
			else if(j!=-mapHeightU&&!mp.cell(j,i).occupied()&&h[i]){
				if(mp.cell(j-1,i).occupied()) now=2.5,sub=1;
				score-=now,now-=sub,sub/=1.666;
				if(mp.cell(j-1,i).occupied()&&(j==environment.fieldHeight-1||mp.cell(j+1,i).occupied())){
					if((i==0||mp.cell(j,i-1).occupied())&&(i==environment.fieldWidth-1||mp.cell(j,i+1).occupied())) score-=5;
					else if((i<2||mp.cell(j,i-1).occupied()||mp.cell(j,i-2).occupied())&&(i>=environment.fieldWidth-2||mp.cell(j,i+1).occupied()||mp.cell(j,i+2).occupied())) score-=4;
					else score-=3;
				}
				else if(mp.cell(j-1,i).occupied()&&(i<2||mp.cell(j,i-1).occupied()||mp.cell(j,i-2).occupied())&&(i>=environment.fieldWidth-2||mp.cell(j,i+1).occupied()||mp.cell(j,i+2).occupied())) score-=4;
			}
		}
		max_h=std::max(max_h,h[i]),min_h=std::min(min_h,h[i]);
		score-=h[i]*0.15;
		if(i) score-=abs(h[i]-h[i-1])*0.2,cntstair[std::min(abs(h[i]-h[i-1]),3)]++;
	}
	if(max_h>=environment.fieldHeight*0.6) score-=(max_h-environment.fieldHeight*0.6)*2.5;
	if(max_h==environment.fieldHeight-4) score-=6;
	if(max_h==environment.fieldHeight-3) score-=10;
	if(max_h==environment.fieldHeight-2) score-=10000;
	if(max_h>=environment.fieldHeight-1) score-=(max_h-(environment.fieldHeight-1)+1)*20000;
	int max_hole2_pos=0,max_hole2=0;
	for(int i=0;i<environment.fieldWidth;i++){
		int mhole=0;
		int now=environment.fieldHeight-h[i];
		while(now>-mapHeightU&&(i==0||mp.cell(now-1,i-1).occupied())&&(i==environment.fieldWidth-1||mp.cell(now-1,i+1).occupied())) now--,mhole++;
		if(mhole>=max_hole2) max_hole2=mhole,max_hole2_pos=i;
		mhole=1e9;
		if(i) mhole=std::min(mhole,h[i-1]);
		if(i!=environment.fieldWidth-1) mhole=std::min(mhole,h[i+1]);
		mhole=std::max(0,mhole-h[i]);
		if(mhole>=3) score-=std::pow(2.1,mhole-5)+0.5;
		max_hole=std::max(max_hole,mhole);
	}
	score-=(h[max_hole2_pos]-min_h)*0.15;
	if(max_hole2==0) score-=2;
	if(max_hole2==1) score-=1.2;
	if(max_hole2==2) score-=0.8;
	if(max_hole2==3) score-=0.3;
	if(max_hole2>=3) score+=std::pow(2.1,max_hole2-5)+0.5;
	score+=max_hole2*0.4;
	if(max_hole2_pos&&max_hole2_pos!=environment.fieldWidth-1) score+=max_hole2*0.2;
	if(max_hole>environment.fieldHeight/2) score-=(max_hole-environment.fieldHeight/2)*0.4;
	if(cntstair[0]<4) score-=0.1*(4-cntstair[0]);
	if(cntstair[1]>5) score-=0.12*(cntstair[1]-5);
	if(cntstair[2]>3) score-=0.17*(cntstair[2]-3);
	if(cntstair[3]>1) score-=1*(cntstair[3]-1);
	if(cntstair[1]==0) score-=0.05;
	if(cntstair[2]==0) score-=0.05;
	std::sort(h,h+environment.fieldWidth);
	score-=std::pow(1.2,max_h-h[environment.fieldWidth-4]);
	return score;
}

void evaluate(const Bot::Environment& environment,Runtime::Playfield mp,const Piece::Definition& B,int x,int y,int type,double &score){
	/* A Naive Example
		score=x;
		return;
	*/
	for(int i=0;i<4;i++) mp.set_cell(x+B.cells(Runtime::Rotation::from_index(type))[i][0],y+B.cells(Runtime::Rotation::from_index(type))[i][1],Runtime::BoardCell::piece(B.color_id()));
	int tag[205],cnt_clear=0,pc_flag=1;
	int mapHeightU=std::min(Runtime::kHiddenRowCapacity,8);
	for(int i=-mapHeightU;i<environment.fieldHeight;i++){
		tag[i+mapHeightU]=1;
		for(int j=0;j<environment.fieldWidth;j++){
			if(!mp.cell(i,j).occupied()){tag[i+mapHeightU]=0;break;}
		}
		for(int j=1;j<environment.fieldWidth&&pc_flag;j++) if(mp.cell(i,j).occupied()!=mp.cell(i,0).occupied()) pc_flag=0;
		cnt_clear+=tag[i+mapHeightU];
	}
	int now=environment.fieldHeight;
	if(environment.antiGravity){
		for(int i=environment.fieldHeight-1;i>=-mapHeightU;i--) if(tag[i+mapHeightU]){
			for(int j=0;j<environment.fieldWidth;j++) mp.set_cell(i,j,Runtime::BoardCell::empty());
		}
	}
	else{
		for(int i=environment.fieldHeight-1;i>=-mapHeightU;i--){
			now--;
			while(now+mapHeightU>=1&&tag[now+mapHeightU]) now--;
			now=std::max(now,1-mapHeightU);
			mp.copy_row(i,now);
		}
	}
	if(cnt_clear==1) score-=1.5;
	if(cnt_clear==2) score-=3;
	if(cnt_clear==3) score-=4;
	if(cnt_clear==4) score+=4.0;
	if(pc_flag) score+=1e9;
	score+=terrain(environment,mp);
}

double ansscore;
int ansx,ansy,anstype,ansrot;
struct node{
	int x,y,type,rot_op,step;
}q[400005];
int headq,tailq;
int st[205][105][4];
void update(int x,int y,int type,int rot_op,int step){
	if(rot_op>st[x+Runtime::kHiddenRowCapacity][y+1][type]){
		st[x+Runtime::kHiddenRowCapacity][y+1][type]=rot_op,q[++tailq]=node{x,y,type,rot_op,step};
	}
}
void bfs(const Bot::Environment& environment,Runtime::Playfield &mp,const Piece::Definition& B,int sx,int sy,int stype){
	ansscore=-1e9;
	for(int i=0;i<environment.fieldHeight+Runtime::kHiddenRowCapacity;i++) for(int j=0;j<=environment.fieldWidth;j++) for(int k=0;k<4;k++) st[i][j][k]=-1;
	headq=1,tailq=0,update(sx,sy,stype,0,0);
	while(headq<=tailq){
		node u=q[headq++];
		int x=u.x,y=u.y,type=u.type,step=u.step;
		if(u.rot_op!=st[x+Runtime::kHiddenRowCapacity][y+1][type]) continue;
		int xx=x;
		while(BotPiece::can_place(B,xx+1,y,type,mp)) xx++;
		if(xx==x || environment.movement.disableSoftDrop){
			const int landingRotation=xx==x ? u.rot_op : 0;
			double score=0;
			evaluate(environment,mp,B,xx,y,type,score);
			if(score>ansscore||(fabs(score-ansscore)<=1e-8&&ansrot<landingRotation)) ansscore=score,ansx=xx,ansy=y,anstype=type,ansrot=landingRotation;
		}
		if(!environment.movement.disableSoftDrop && xx!=x) update(xx,y,type,0,step+1);
		for(int direction:{-1,1}){
			if(!environment.movement.allows_horizontal(direction)) continue;
			int yy=y;
			while(BotPiece::can_place(B,x,yy+direction,type,mp)){
				yy+=direction;
				if(!environment.wall_only_horizontal()) break;
			}
			if(yy!=y) update(x,yy,type,0,step+1);
		}
		int rotate_op;
		for(int i=1;i<=3;i++){
			if(!environment.movement.allows_rotation(i)) continue;
			int tx=x,ty=y,ttype=(type+i)%4;
			if(BotPiece::try_rotate(B,tx,ty,type,ttype,mp,rotate_op)) update(tx,ty,ttype,rotate_op+1,step+1);
		}
	}
}
bool init(const Bot::Environment& environment){
	return bool(availability(environment));
}
int getpos(const Bot::Environment& environment,const Runtime::PlayerSessionState& player,Runtime::Playfield mp,std::vector<int>nex,const std::vector<int>& unseenBagPieces,int sx,int sy,int stype,int hold,int &ans_x,int &ans_y,int &ans_type,int &ans_rot){
    if(Bot::QuickPlaySearch::enabled(environment)) {
        using namespace Bot::QuickPlaySearch;
        const auto shape=[&](const std::vector<std::uint32_t>& rows,int width,int height,int,int) {
            Runtime::Playfield field;field.initialize(width,height);
            for(int y=-Runtime::kHiddenRowCapacity;y<height;++y)for(int x=0;x<width;++x)
                if(rows[height-1-y]&(std::uint32_t{1}<<x))field.set_cell(y,x,Runtime::BoardCell::piece(1));
            auto geometry=environment;geometry.fieldWidth=width;geometry.fieldHeight=height;
            return 512*terrain(geometry,field);
        };
        const auto native=[&](const NativeBaseline& baseline) {
            Decision result;if(!availability(baseline.environment))return result;
            int row=sx+baseline.rowOffset,col=sy,r=stype,k=0;
            result.selected=getpos(baseline.environment,*baseline.player,*baseline.field,nex,unseenBagPieces,
                row,col,r,hold,row,col,r,k);
            result.pose={{row-baseline.rowOffset,col},Runtime::Rotation::from_index(r)};
            result.rotation=k>0?Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(k-1)):Runtime::RotationRecord{};
            return result;
        };
        const auto result=adapt(Profile::Candy,environment,player,mp,nex,unseenBagPieces,
            {{sx,sy},Runtime::Rotation::from_index(stype)},hold,native,shape);
        ans_x=result.pose.position.row;ans_y=result.pose.position.col;ans_type=result.pose.rotation.index();
        ans_rot=result.rotation.rotated()?result.rotation.kick().index()+1:0;return result.selected;
    }
	(void)unseenBagPieces; // Interface-only metadata; strategy intentionally unchanged.
	(void)player;
	bfs(environment,mp,BotPiece::definition(nex[0],environment),sx,sy,stype);
	ans_x=ansx,ans_y=ansy,ans_type=anstype,ans_rot=ansrot;
	double tmp=ansscore;
	if(hold!=-2 && (hold!=-1 || nex.size()>1)){
		if(nex.size()==1) return nex[0];
		if(hold==-1) hold=nex[1];
		sx=-2,sy=(environment.fieldWidth-1)/2,stype=0;
		Piece::Definition holdPiece=BotPiece::definition(hold,environment);
		if(BotPiece::can_place(holdPiece,sx,sy,stype,mp)){
			if(fabs(environment.gravityIntervalSeconds)<1e-8) while(BotPiece::can_place(holdPiece,sx+1,sy,stype,mp)) sx++;
			bfs(environment,mp,holdPiece,sx,sy,0);
			if(ansscore>tmp){
				ans_x=ansx,ans_y=ansy,ans_type=anstype,ans_rot=ansrot;
				return hold;
			}
		}
	}
	return nex[0];
}

}
