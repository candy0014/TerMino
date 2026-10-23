#include "tetr/util/bit_ops.h"
#include "tetr/bot/bot_while1.h"
#include "tetr/bot/quick_play_search.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/rules/opener_phase.h"
#include "tetr/engine/movement_presentation.h"
#include <functional>

#include <cassert>
#include <cmath>
#include <ctime>
#include <random>
#include <limits>

#include "bot_piece_access.h"
#include <vector>
#include <utility>
#include <algorithm>

namespace Bot_while1{

std::mt19937 rnd(time(0));

int current_qp_last_operation(const Runtime::PlayerSessionState& player){
	if(!player.gameplay.quickPlay.lastOperation) return 8191;
	const auto& operation=*player.gameplay.quickPlay.lastOperation;
	return operation.pieceType.index() |
	       (operation.linesCleared<<8) |
	       (static_cast<int>(operation.spin)<<11) |
	       (static_cast<int>(operation.mini)<<12);
}

double sta_val=1;
double lval=1;
double bval=1;
const double APL=1.1;
#define pii pair<int,int>
#define a first
#define b second
const int I=0,J=1,L=2,O=3,S=4,T=5,Z=6;
char SS[10]="IJLOSTZ";
int a[1000100];
int M=0;
const int N=20;
int tag[N][1005];
int q[7][4][4][2]=
{
	{{0,-1,0,0,0,1,0,2},{-1,1,0,1,1,1,2,1},{1,-1,1,0,1,1,1,2},{-1,0,0,0,1,0,2,0}},
	{{0,0,0,-1,0,1,-1,-1},{0,0,-1,0,1,0,-1,1},{0,0,0,-1,0,1,1,1},{0,0,-1,0,1,0,1,-1}},
	{{0,0,0,-1,0,1,-1,1},{0,0,-1,0,1,0,1,1},{0,0,0,-1,0,1,1,-1},{0,0,-1,0,1,0,-1,-1}},
	{{0,0,-1,0,-1,1,0,1},{0,0,-1,0,-1,1,0,1},{0,0,-1,0,-1,1,0,1},{0,0,-1,0,-1,1,0,1}},
	{{0,0,-1,0,-1,1,0,-1},{0,0,0,1,1,1,-1,0},{0,0,0,1,1,0,1,-1},{0,0,1,0,0,-1,-1,-1}},
	{{0,0,0,1,0,-1,-1,0},{0,0,-1,0,1,0,0,1},{0,0,0,1,0,-1,1,0},{0,0,-1,0,1,0,0,-1}},
	{{0,0,0,1,-1,0,-1,-1},{0,0,0,1,-1,1,1,0},{0,0,1,0,0,-1,1,1},{0,0,-1,0,0,-1,1,-1}}
};
unsigned long long hsh(unsigned long long x) {
    // http://xorshift.di.unimi.it/splitmix64.c
    x += 0x9e3779b97f4a7c15;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
    x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
    return x ^ (x >> 31);
}
const int MaxH=32;
std::vector<int>nxt;
struct board
{
	const Bot::Environment* environment=nullptr;
	std::vector<int>a;
	int b,c;double s;int g,l,n;
	int h,p;
	//b2b,combo,sum,garbage,line,nuts
	//hold,pointer
	int tag;//0~9:pre 10:swap 11~14 row 15~16:rot1 17~18:rot2
	int las;
	std::vector<int>v;
	Runtime::GarbageCancellationState cancellation;
	int placedPieces=0,sentAttack=0;
	board(){a.resize(MaxH);b=c=-1;s=g=0;h=-1,p=0;las=8191;n=0;v.resize(7);}
	double vall;
	unsigned long long H;
	double val()
	{
		H=0;
		H=hsh(H+c);
		H=hsh(H+g);
		H=hsh(H+l);
		H=hsh(H+h);
		H=hsh(H+p);
		H=hsh(H+n);
		H=hsh(H+placedPieces);H=hsh(H+sentAttack);
		H=hsh(H+std::hash<double>{}(cancellation.cancellationFraction));
		H=hsh(H+std::hash<double>{}(cancellation.outgoingFraction));
		for(int lines:v)H=hsh(H+lines);
		for(int i=0;i<29;i++)H=hsh(H+a[i]);
		for(int i=0;i<7;i++)H=hsh(H+v[i]);
		auto P=findtspin();
		if(environment->randomPieces)P={0,0};
		if(P.a)
		{
			int x=P.a,y=P.b;
			if(y<0)y=~y;
			a[x]^=5<<(y-1);
		}
		int U=0;
		int res=0;
		int y[11]={};
		int DHGB=0;
		int ihole=0;
		int iholeval=0;
		for(int i=MaxH-5;i>=0;i--)
		{
			U|=a[i];
			res+=Util::popcount32(a[i]^U);
			if(a[i]!=U)U=1023;
			if(Util::popcount32(a[i])==1)iholeval=a[i]^1023;
			if((a[i]&iholeval)!=0)iholeval=0;
			if(iholeval&&ihole>=3)DHGB+=Util::popcount32(a[i]^1023^iholeval)*ihole;
		}
		int p=0;int o=l;
		int h[11]={},l[11]={},r[11]={};
		int bottom=std::max(g-3,(int)0);
		for(int i=0;i<=10;i++)h[i]=y[i]=bottom;
		for(int i=environment->fieldHeight-1;i>=bottom;i--)
		{
			for(int j=0;j<10;j++)if(a[i]>>j&1)h[j]=std::max(h[j],i);else y[j]=std::min(y[j],i);
		}
		int isV=0;
		int top=std::min(bottom+3,g+2);
		for(int i=bottom;i<top;i++)if(a[i])
		{
			isV+=a[i]==1022||a[i]==511||a[i]==510;
		}
		int w2=0;
		for(int i=0;i<7;i++)w2=std::max(w2,std::min(h[i],h[i+3])-std::max(h[i+1],h[i+2]));
		w2=std::max(w2,h[2]-std::max(h[1],h[0]));w2=std::max(w2,h[7]-std::max(h[8],h[9]));
		int sum=0;
		if(n)for(int i=0;i<10;i++)sum+=(h[i]-y[i])*(h[i]-y[i]-1);
		else for(int i=environment->fieldHeight-1;i>=bottom;i--)
		{
			for(int j=0;j<10;j++)if(!(a[i]>>j&1))if(i<h[j])sum+=(h[j]-i)*(h[j]-i-1);
		}
		if(environment->randomPieces)sum*=5;
		int ht=0;for(int i=0;i<10;i++)ht=std::max(ht,h[i]);
		if(n)sum*=exp((ht-g-4)*0.2);
		for(int i=0;i<10;i++)l[i]=r[i]=h[i];
		for(int i=1;i<10;i++)l[i]=std::min(l[i],l[i-1]);
		for(int i=8;i>=0;i--)r[i]=std::min(r[i],r[i+1]);
		for(int i=1;i<9;i++)p=std::max(p,std::min(h[i]-l[i],h[i]-r[i]));
		for(int i=0;i<10;i++)p=std::max(p,std::max(h[i]-h[0]-2,h[i]-h[9]-2));
		double Ma1=0,Sum=0;
		for(int i=0;i<10;i++)
		{
			Sum+=std::min(i==0?10000:abs(h[i-1]-h[i]),i==9?10000:abs(h[i+1]-h[i]));
			Ma1=std::max(Ma1,(double)std::min(i==0?10000:h[i-1]-h[i],i==9?10000:h[i+1]-h[i]));
		}
		Sum-=Ma1;
		double pen[12]={0,0,1,2,3.5,5,7,10,15,20,1000000};
		double bpen[6]={2,0.8,0.6,0.6,0.6,1.0};
		if(n)bpen[0]=bpen[1]=bpen[2]=bpen[3]=bpen[4]=bpen[5]=0.8;
		if(environment->randomPieces)
		{
			for(int i=0;i<12;i++)pen[i]*=0.5;
			p/=2,w2=0,bpen[0]=bpen[1]=bpen[2]=bpen[3]=bpen[4]=bpen[5]=1;
		}
		if(P.a)
		{
			int x=P.a,y=P.b;
			if(y<0)y=~y;
			a[x]^=5<<(y-1);
		}
		double L=1;
		L*=exp((std::max(ht,(int)10)-10)*0.2);
		L*=exp((std::max(g,(int)5)-5)*0.2);
		if(environment->quickPlayNoHold||environment->randomPieces)
		{
			L=1;
			L*=exp((std::max(ht,(int)5)-5)*0.2);
			L*=exp((std::max(ht,(int)8)-8)*0.3);
			L*=exp((std::max(ht,(int)12)-12)*0.6);
		}
		double value=0;
		double apl=APL;

		if(environment->quickPlayAllSpin&1)apl*=1.3;
		if(environment->quickPlayAllSpin==2)apl*=1.7;
		if(environment->quickPlayNoHold||environment->randomPieces)apl=(apl+0.3)/2;
		if(environment->randomPieces)apl/=2;
		if(environment->randomPieces)b=0;
		if(environment->quickPlayAllSpin==2)isV*=3;
		double stv=sta_val*(environment->quickPlayAllSpin?1.5:1);
		double vslist[10]={1,0.71,0.64,0.71};
		double vs=vslist[environment->quickPlayAllSpin];
		if(environment->quickPlayFool==1)vs*=0.5;
		if(environment->quickPlayFool==2)vs*=0.1;
		if(environment->quickPlayFool==3||environment->quickPlayFool==4)
		{
			double vss[15]={1,1,1,1,1,0.9,0.7,0.5,0.3,0.1,0.0001};
			vs*=vss[environment->quickPlayFloor];
		}
		value=vs*(s+stv*((b?b+2:0)*bval)-(environment->garbageModel!=11&&(environment->quickPlayAllSpin==2||environment->quickPlayMessy==2))*g-((DHGB*DHGB)+(w2>=0?w2*(w2-1):0)+(environment->randomPieces)*Sum+isV*3+(bpen[std::min(b,(int)4)+1]*res)+lval*apl*o+pen[std::min(p,(int)10)]+10)*L);
		return vall=value+rnd()/1e13;
	}
	std::pair<int,int> findtspin()
	{
		std::pair<int,int>P={100,100};
		for(int i=2;i<10;i++)
		{
			int o=22;
			while(~o&&!(a[o]>>i&1))o--;
			if(o==-1)continue;
			bool f=1;
			for(int j=o;j<23;j++)if(a[j]>>(i-1)&1)f=0;
			f&=(a[o]>>(i-2)&1)&&(a[o+2]>>(i-2)&1)&&!(a[o+1]>>(i-2)&1);
			if(f)P=std::min(P,{o+1,i-1});
		}
		for(int i=0;i<8;i++)
		{
			int o=22;
			while(~o&&!(a[o]>>i&1))o--;
			if(o==-1)continue;
			bool f=1;
			for(int j=o;j<23;j++)if(a[j]>>(i+1)&1)f=0;
			f&=(a[o]>>(i+2)&1)&&(a[o+2]>>(i+2)&1)&&!(a[o+1]>>(i+2)&1);
			if(f)P=std::min(P,{o+1,~(i+1)});
		}
		if(P==std::pair<int,int>{100,100})return {0,0};
		return P;
	}
	void clear(int cur,bool spin=0,bool t=0)
	{
		if(environment->quickPlayNoHold==2)t=0;
		int allspin=environment->quickPlayAllSpin;
		if(allspin)t|=spin&&cur!=T;
		int cg=0,cn=0;
		int cnt=0;
		{
			std::vector<int>v;
			for(int i=0;i<MaxH;i++)if(i>=environment->fieldHeight-environment->fieldHeight&&a[i]==1023)cg+=i<g,cn+=g<=i&&i<n+g,cnt++;else v.push_back(a[i]);
			for(int i=0;i<(int)v.size();i++)a[i]=v[i];
		}
		if(cnt)
		{
			int bb=b;
			int add=0;
			bool is=spin||cnt>=4;
			if(is==1&&cg)add+=1;
			g-=cg;n-=cn;
			const bool perfectClear=a[0]==0;
			if(perfectClear)is=1,add+=5;
			int bas=t?cnt*2:cnt-1+(cnt>=4);
			if(!spin&&allspin==2)bas=0;
			////////////////////////////////////////////////////////tspins
			if(is)b++;else add+=(b>=4?b-3:0),b=-1;
			c++;
			bas+=(b>=1);
			if(bas==0)add+=(c>=2)+(c>=6)+(c>=16);
			else add+=bas*(4+c)/4;
			double pen[5]={0,1,0,-1,-2};
			int ATK=add;
			const int pc=environment->legacyOpenerPhase ? 0 : std::min(ATK,perfectClear ? 5 : 0);
			ATK=0;
			for(const int amount:{add-pc,pc}) {
				if(amount<=0)continue;
				std::int64_t pending=0;for(int lines:v)pending+=lines;
				const bool doubled=Rules::opener_phase_double_cancel(placedPieces,amount,
					static_cast<int>(std::min<std::int64_t>(pending,2147483647)),environment->openerDoubleCancelEnabled,
					environment->openerPhase,sentAttack,environment->legacyOpenerPhase);
				const auto result=Rules::cancel_garbage_amount(pending,amount,environment->cancellationMultiplier,doubled,cancellation);
				auto budget=pending-result.remaining;
				for(int& lines:v) {const int consumed=static_cast<int>(std::min<std::int64_t>(budget,lines));lines-=consumed;budget-=consumed;}
				ATK+=result.outgoingAttack;sentAttack+=result.outgoingAttack;
			}
			if(environment->garbageModel==11)
			{
				if(!is)
				{
					if(bb>=4)s-=3;
					else s-=bb+2;
				}
				if(environment->quickPlayMessy==2)s-=2,s+=ATK;
			}
			if(environment->garbageModel==10)
			{
				if(!is)
				{
					if(bb>=4)s-=3;
					else s-=bb+2;
				}
			}
			s+=add+cn*10+cg*(0.1+(environment->garbageModel!=11&&(environment->quickPlayAllSpin==2||environment->quickPlayMessy==2)))-(environment->quickPlayMessy==2)*pen[cnt];
			l+=cnt;
		}
		else c=-1;
		if(cnt||(spin&&environment->quickPlayAllSpin==2&&b>=4));
		else
		{
			if(environment->quickPlayAllSpin==2)
			{
				s-=v[0]*2.0;
				if(v[0]>6)s-=(v[0]-6)*2.0;
				if(v[0]>10)s-=(v[0]-10)*2.0;
				if(v[0]>15)s-=(v[0]-15)*10.0;
			}
			else
			{
				if(v[0]>6)s-=v[0]-6;
				if(v[0]>12)s-=v[0]-12;
				if(v[0]>16)s-=v[0]-16;
			}
			v[0]-=std::min(v[0],environment->garbageCap);
			if(std::all_of(v.begin(),v.end(),[](int lines){return lines==0;}))cancellation.cancellationFraction=0;
		}
		v[1]+=std::min(v[0],std::numeric_limits<int>::max()-v[1]);
		for(int i=1;i<7;i++)v[i-1]=v[i];
		if(!cnt&&!spin)return;
		if(allspin==2&&b>=4&&!cnt&&spin)s+=2;
		int res=cur|(cnt<<8)|((int)spin<<11)|((int)t<<12);
		if(allspin==2)
		{
			int r=spin?cnt:-1;
			int l=(las>>11&1)?(las>>8&7):-1;
			if(l==r)s=-1e9;
			las=res;
		}
		else if(allspin)
		{
			if(res==las)s-=3;
			else if(!(res>>11&1)&&(res|255)==(las|255))s-=3;
			las=res;
		}
	}
	void drop(int x,int d,int o,int y,int spin=0,int t=0)
	{
		if(x!=nxt[p])
		{
			if(h==-1)h=nxt[p++];
			else h=nxt[p];
		}
		p++;
		++placedPieces;
		const auto definition=BotPiece::definition(x,*environment);
		for(auto P:definition.cells(Runtime::Rotation::from_index(d)))a[o-P[0]]|=1<<(y+P[1]);
		// for(int i=0;i<4;i++)a[o-q[x][d][i][0]]|=1<<(y+q[x][d][i][1]);
		clear(x,spin,t);
		val();
	}
};
bool operator<(board a,board b)
{
	return a.vall>b.vall;
}
bool cmp(board a,board b)
{
	return a.H<b.H;
}
int hld=-1,cur;
int n;
board v[500010],w[500010];
int p[500010],u[500010];
int cv,cw,cu;

struct node{
	int x,y,type,rot_op,step;
}qu[400005];
int headq,tailq;
int st[205][105][4][3];
void update(int x,int y,int type,int rot_op,int step,int name){
	int o=0;
	if(name==5)
	{
		if(rot_op==0)o=0;
		else if(rot_op==2)o=2;
		else o=1;
	}
	if(st[x+Runtime::kHiddenRowCapacity][y+1][type][o]==-1){
		st[x+Runtime::kHiddenRowCapacity][y+1][type][o]=1,qu[++tailq]=node{x,y,type,rot_op,step};
	}
}
void bfs(const Bot::Environment& environment,board &mp,const Piece::Definition &B,int sx,int sy,int stype,int pre,int flag_hold){
	if(!BotPiece::bitboard_can_place(B,sx,sy,stype,mp.a,environment.fieldWidth,environment.fieldHeight)) return;
	for(int i=0;i<environment.fieldHeight+Runtime::kHiddenRowCapacity;i++) for(int j=0;j<=environment.fieldWidth;j++) for(int k=0;k<4;k++) for(int l=0;l<3;l++) st[i][j][k][l]=-1;
    const bool instant=environment.naturalGravityEnabled && environment.gravityIntervalSeconds==0.0;
    const Engine::MovementController::CanPlace canPlace=[&](Piece::Pose pose) {
        return BotPiece::bitboard_can_place(B,pose.position.row,pose.position.col,pose.rotation.index(),
            mp.a,environment.fieldWidth,environment.fieldHeight);
    };
    const Engine::MovementController::TryRotate tryRotate=[&](Piece::Pose& pose,Runtime::Rotation rotation,Piece::KickIndex& kick) {
        int index=0,row=pose.position.row,col=pose.position.col;
        if(!BotPiece::bitboard_try_rotate(B,row,col,pose.rotation.index(),rotation.index(),
            mp.a,environment.fieldWidth,environment.fieldHeight,index))return false;
        pose={{row,col},rotation};kick=Piece::KickIndex::from_index(index);return true;
    };
	const auto enqueue=[&](int x,int y,int type,int rotation,int step) {
		const int before=x;
		if(instant)while(BotPiece::bitboard_can_place(B,x+1,y,type,mp.a,environment.fieldWidth,environment.fieldHeight))++x;
		if(x!=before)rotation=0;
		update(x,y,type,rotation,step,BotPiece::logical_type(B,environment));
	};
	headq=1,tailq=0,update(sx,sy,stype,0,0,0);
	while(headq<=tailq){
		node u=qu[headq++];
		int x=u.x,y=u.y,type=u.type,step=u.step,rot_op=u.rot_op;
		const int pathX=x,pathRotation=rot_op;
		if(x<=-6)continue;
		if(instant){
			while(BotPiece::bitboard_can_place(B,x+1,y,type,mp.a,environment.fieldWidth,environment.fieldHeight)) x++,rot_op=0;
		}
		if(environment.movement.disableSoftDrop){
			while(BotPiece::bitboard_can_place(B,x+1,y,type,mp.a,environment.fieldWidth,environment.fieldHeight)) x++;
			if(x!=pathX) rot_op=0;
		}
		if(!BotPiece::bitboard_can_place(B,x+1,y,type,mp.a,environment.fieldWidth,environment.fieldHeight)){
			board C=mp;
			C.tag=pre;
			C.tag|=((environment.fieldHeight-x-1)<<10);
			C.tag|=((y+1)<<15);
			C.tag|=(type<<19);
			C.tag|=(std::min(6,rot_op)<<21);
			C.tag|=(flag_hold<<24);
			bool spin=0,t=0;
			if(!BotPiece::bitboard_can_place(B,x-1,y,type,mp.a,environment.fieldWidth,environment.fieldHeight)&&!BotPiece::bitboard_can_place(B,x,y-1,type,mp.a,environment.fieldWidth,environment.fieldHeight)&&!BotPiece::bitboard_can_place(B,x,y+1,type,mp.a,environment.fieldWidth,environment.fieldHeight)) spin=1;
			if(BotPiece::logical_type(B,environment)==5){
				int _dir[4][2]={-1,-1,-1,1,1,1,1,-1},cnt1=0,cnt2=0;
				for(int i=0;i<4;i++){
					int tx=x+_dir[i][0],ty=y+_dir[i][1];
					if(tx>=environment.fieldHeight||ty<0||ty>=environment.fieldWidth||(mp.a[environment.fieldHeight-tx-1]>>ty)&1){
						cnt1++;
						if(i==type||i==(type+1)%4) cnt2++;
					}
				}
				if(cnt1>=3&&(cnt2==2||rot_op==5)) t=1,spin=1;
				else if(cnt1>=3&&rot_op)spin=1;
			}
			C.drop(BotPiece::logical_type(B,environment),type,environment.fieldHeight-x-1,y,spin,t);
			w[cw++]=C;
		}
        x=pathX; rot_op=pathRotation;
        if(instant) {
            // Use the host's frame transitions: even a slide or a zero-DAS
            // repeat must land after every horizontal cell before moving again.
            for(const char code:Engine::kMovementSearchCommands) {
                const auto command=Engine::decode_movement_script_command(code);
                if(!Engine::movement_script_command_allowed(command,environment.movement,
                    environment.wall_only_horizontal(),environment.naturalGravityEnabled,true,true))continue;
                Engine::MovementState candidate;
                candidate.pose={{x,y},Runtime::Rotation::from_index(type)};
                candidate.lastRotation=rot_op ? Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(rot_op-1))
                    : Runtime::RotationRecord::none();
                const auto effect=Engine::apply_movement_script_command(candidate,command,canPlace,tryRotate,
                    environment.wall_only_horizontal(),environment.arrSeconds==0,environment.movement,true,true);
                if(effect.changed)enqueue(candidate.pose.position.row,candidate.pose.position.col,candidate.pose.rotation.index(),
                    candidate.lastRotation.rotated() ? candidate.lastRotation.kick().index()+1 : 0,step+1);
            }
            continue;
        }
        if(!environment.movement.disableSoftDrop){
			int xx=x;
			while(BotPiece::bitboard_can_place(B,xx+1,y,type,mp.a,environment.fieldWidth,environment.fieldHeight)) xx++;
			if(xx!=x) enqueue(xx,y,type,0,step+1);
		}
        if(environment.partial_descent_available() &&
			BotPiece::bitboard_can_place(B,x+1,y,type,mp.a,environment.fieldWidth,environment.fieldHeight)) enqueue(x+1,y,type,0,step+1);
		for(int direction:{-1,1}){
			if(!environment.movement.allows_horizontal(direction)) continue;
			int yy=y,xx=x;
			while(BotPiece::bitboard_can_place(B,xx,yy+direction,type,mp.a,environment.fieldWidth,environment.fieldHeight)){
				yy+=direction;
				if(!environment.wall_only_horizontal()) break;
			}
			if(yy!=y) enqueue(xx,yy,type,0,step+1);
		}
		int rotate_op;
		for(int i=1;i<=3;i++){
			if(!environment.movement.allows_rotation(i)) continue;
			int tx=x,ty=y,ttype=(type+i)%4;
			if(BotPiece::bitboard_try_rotate(B,tx,ty,type,ttype,mp.a,environment.fieldWidth,environment.fieldHeight,rotate_op)) {
				enqueue(tx,ty,ttype,rotate_op+1,step+1);
			}
		}
	}
}
bool init(const Bot::Environment& environment){
	return bool(availability(environment));
}
int getpos(const Bot::Environment& environment,const Runtime::PlayerSessionState& player,Runtime::Playfield mp,std::vector<int>nex,const std::vector<int>& unseenBagPieces,int sx,int sy,int stype,int hold,int &ans_x,int &ans_y,int &ans_type,int &ans_rot)
{
    if(Bot::QuickPlaySearch::enabled(environment)) {
        if(Bot::QuickPlaySearch::native_policy_compatible(environment) &&
            availability(Bot::QuickPlaySearch::native_environment(environment))) {
            if(auto baseline=Bot::QuickPlaySearch::native_baseline(Bot::QuickPlaySearch::Profile::While1,
                environment,player,mp)) {
                const int selected=getpos(baseline->environment,*baseline->player,*baseline->field,nex,unseenBagPieces,
                    sx+baseline->rowOffset,sy,stype,hold,ans_x,ans_y,ans_type,ans_rot);
                ans_x-=baseline->rowOffset;
                const auto record=ans_rot>0?Runtime::RotationRecord::from_kick(
                    Piece::KickIndex::from_index(ans_rot-1)):Runtime::RotationRecord{};
                if(Bot::QuickPlaySearch::native_answer_valid(environment,player,mp,nex,
                    {{sx,sy},Runtime::Rotation::from_index(stype)},hold,selected,
                    {{ans_x,ans_y},Runtime::Rotation::from_index(ans_type)},record))return selected;
            }
        }
        sta_val=lval=bval=1;
        const auto shape=[&](const std::vector<std::uint32_t>& rows,int width,int height,int garbage,int b2b) {
            auto geometry=environment;geometry.fieldWidth=width;geometry.fieldHeight=height;
            board B;B.environment=&geometry;B.g=garbage;B.b=std::max(0,b2b-1);B.c=-1;B.s=0;B.l=0;B.n=0;
            for(int i=0;i<std::min<int>(MaxH,rows.size());++i)B.a[i]=rows[i];
            return B.val()*512;
        };
        const auto result=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::While1,
            environment,player,mp,nex,unseenBagPieces,{{sx,sy},Runtime::Rotation::from_index(stype)},hold,shape);
        if(result.selected>=0) {
            ans_x=result.pose.position.row;ans_y=result.pose.position.col;ans_type=result.pose.rotation.index();
            ans_rot=result.rotation.rotated()?result.rotation.kick().index()+1:0;
            return result.selected;
        }
        ans_x=sx;ans_y=sy;ans_type=stype;ans_rot=0;
        if(nex.empty())return -1;
        const auto piece=BotPiece::definition(nex.front(),environment);
        while(piece.can_place(Piece::Pose{{ans_x+1,ans_y},Runtime::Rotation::from_index(ans_type)},mp))++ans_x;
        return nex.front();
    }
	(void)unseenBagPieces; // Interface-only metadata; strategy intentionally unchanged.
	M=std::max(1,environment.autoplaySearchWidth);
	// fout<<"getpos"<<std::endl;
	std::deque<Runtime::GarbagePacket> bf=player.garbage.buffer;
	double tim=environment.nowSeconds;
	std::vector<int>vv(7);
	while(bf.size())
	{
		auto o=bf.front();bf.pop_front();
		const double secondsPerPiece=environment.secondsPerPiece>0 ? environment.secondsPerPiece :
			(environment.autoplayPps>0 ? 1.0/environment.autoplayPps : .1);
		int T=static_cast<int>(std::clamp(std::ceil((o.arrival_time().seconds()+environment.incomingGarbageDelaySeconds-tim)/secondsPerPiece),0.0,6.0));
		if(T<0)T=0;
		if(T>6)T=6;
		vv[T]+=std::min(std::max(0,o.line_count()),std::numeric_limits<int>::max()-vv[T]);
	}
	// for(int i=0;i<7;i++)fout<<vv[i]<<' ';fout<<std::endl;
	board now;now.environment=&environment;now.las=current_qp_last_operation(player);now.n=0;now.v=vv;
	now.placedPieces=player.gameplay.scoring.placedBlocks;
	now.sentAttack=player.gameplay.scoring.totalOutgoingAttack;
	now.cancellation=player.garbage.cancellation;
	now.b=player.gameplay.scoring.b2b-1,now.c=player.gameplay.scoring.combo-1,now.g=player.gameplay.playfield.garbageHeight,now.l=0;
	now.h=hold,now.p=0;
	lval=1;bval=1;
	for(int i=environment.fieldHeight-1;i>=-5;i--){
		for(int j=0;j<10;j++) if(mp.cell(i,j).occupied()) {
			now.a[environment.fieldHeight-1-i]|=(1<<j);
			if(i<5) lval=0,bval=0;
		}
		for(int j=0;j<10;j++) if(mp.cell(i,j).is_normal_garbage()) now.n=environment.fieldHeight-i;
	}
	int m=(int)nex.size()+(hold!=-1&&environment.holdEnabled);if(environment.quickPlayAllSpin==0&&environment.quickPlayFool==0)nex.emplace_back(5);
	nxt=nex;
	cv=0;
	v[cv++]=now;
	int MM=M;
	for(int i=0;i<m;i++)
	{
		// fout<<"CALC"<<std::endl;
		cw=cu=0;
		sta_val=lval=1;
		for(int i=0;i<cv;i++)
		{
			board B=v[i];
			int cur=B.p==(int)nex.size()?-1:nex[B.p];
			int hld=(B.h==-1)?(B.p<(int)nex.size()-1?nex[B.p+1]:-1):B.h;
			if(!environment.holdEnabled) hld=-1;
			if(B.p==0 && (hold==-2 || (B.h==-1 && environment.nextCount==0))) hld=-1;
			for(int j:{cur,hld})if(j>=0)
			{
				Piece::Definition BL=BotPiece::definition(j,environment);
				const auto metadata=Piece::metadata(BL.type());
				const int rotation=metadata.spawnRotation;
				const auto& origin=metadata.rotationOrigins[rotation];
				const bool currentRoot=B.p==0 && j==cur;
				bfs(environment,B,BL,currentRoot ? sx : -2+origin[0],currentRoot ? sy : (environment.fieldWidth-1)/2+origin[1],
					currentRoot ? stype : rotation,i,j!=cur);
			}
		}
		// fout<<"CALCEND"<<std::endl;
		if(!cw){
			ans_x=sx,ans_y=sy,ans_type=stype,ans_rot=0;
			Piece::Definition BL=BotPiece::definition(nex[0],environment);
			while(BotPiece::can_place(BL,ans_x+1,ans_y,ans_type,mp)) ans_x++;
			return nex[0];
		}
		for(int i=0;i<cw;i++)p[i]=i;
		std::stable_sort(p,p+cw,[&](int x,int y){return cmp(w[x],w[y]);});
		for(int i=0;i<cw-1;i++)
		{
			if(!cmp(w[p[i]],w[p[i+1]]))if(w[p[i]]<w[p[i+1]])p[i+1]=p[i];
		}
		for(int i=0;i<cw-1;i++)if(cmp(w[p[i]],w[p[i+1]]))u[cu++]=(p[i]);
		u[cu++]=(p[cw-1]);
		std::stable_sort(u,u+cu,[&](int x,int y){return w[x]<w[y];});
		cu=std::min(cu,MM);
		for(int j=0;j<cu;j++)
		{
			tag[i][j]=w[u[j]].tag;
		}
		cv=cu;
		for(int i=0;i<cv;i++)v[i]=w[u[i]];
		if(i==0)
		{
		}
		// MM*=0.8;
	}
	int p=m-1,x=0;
	if(environment.quickPlayNoHold==2)
	{
		//x can be not 0
		double Ma=-1e300;
		for(int i=0;i<cv;i++)
		{
			double ans=1e300;
			board B=v[i];
			for(int j=0;j<7;j++)
			{
				cw=0;
				double ma=0;
				Piece::Definition BL=BotPiece::definition(j,environment);
				bfs(environment,B,BL,-2,(environment.fieldWidth-1)/2,0,i,j!=cur);
				for(int i=0;i<cw;i++)ma=std::max(ma,w[i].vall);
				ans=std::min(ans,ma);
			}
			if(ans>Ma)Ma=ans,x=i;
		}
	}
	while(p)x=tag[p][x]&1023,p--;
	int d=tag[p][x];
	ans_x=environment.fieldHeight-1-((d>>10)&31),ans_y=((d>>15)&15)-1,ans_type=(d>>19)&3,ans_rot=(d>>21)&7;
	// fout<<(d>>10&31)<<' '<<(d>>15&15)<<' '<<ans_type<<' '<<(d>>24)<<std::endl;
	if(d>>24){
		assert(environment.holdEnabled&&hold!=-2);
		if(hold==-1){
			if(nex.size()>1) return nex[1];
			return -1;
		}
		return hold;
	}
	return nex[0];
}

}
