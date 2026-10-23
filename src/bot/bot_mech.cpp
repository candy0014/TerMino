#include "tetr/bot/bot_mech.h"
#include "tetr/bot/quick_play_search.h"
#include "tetr/bot/bot_candy0014.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <ctime>

#include "bot_piece_access.h"
#include <vector>
#include <string>
#include <random>

namespace Bot_mech{

const int I=0,J=1,L=2,O=3,S=4,T=5,Z=6;

Piece::Definition blocks[7];
int cur,hld=7;

int Bot_Place(Runtime::Playfield &mp,const Piece::Definition& B,std::string s,int &x,int &y,int &type,int &last_op){
	for(char op:s){
		if(op=='H'){
			while(BotPiece::can_place(B,x+1,y,type,mp)) x++,last_op=0;
			break;
		}
		if(op=='W'){
		}
		if(op=='A'){
			int rotate_op;
			if(BotPiece::try_rotate(B,x,y,type,(type+2)%4,mp,rotate_op)){
				type=(type+2)%4,last_op=rotate_op+1;
			}
		}
		if(op=='X'){
			int rotate_op;
			if(BotPiece::try_rotate(B,x,y,type,(type+1)%4,mp,rotate_op)){
				type=(type+1)%4,last_op=rotate_op+1;
			}
		}
		if(op=='Z'){
			int rotate_op;
			if(BotPiece::try_rotate(B,x,y,type,(type+3)%4,mp,rotate_op)){
				type=(type+3)%4,last_op=rotate_op+1;
			}
		}
		if(op=='S'){
			if(BotPiece::can_place(B,x+1,y,type,mp)){
				x++,last_op=0;
			}
		}
		if(op=='s'){
			while(BotPiece::can_place(B,x+1,y,type,mp)){
				x++,last_op=0;
			}
		}
		if(op=='L'){
			if(BotPiece::can_place(B,x,y-1,type,mp)){
				y--,last_op=0;
			}
		}
		if(op=='l'){
			while(BotPiece::can_place(B,x,y-1,type,mp)){
				y--,last_op=0;
			}
		}
		if(op=='R'){
			if(BotPiece::can_place(B,x,y+1,type,mp)){
				y++,last_op=0;
			}
		}
		if(op=='r'){
			while(BotPiece::can_place(B,x,y+1,type,mp)){
				y++,last_op=0;
			}
		}
	}
	return 0;
}

int node=-1,i;
char ans[20];
int ca;
bool vis[8];int a[5];
int *b=a;

void act(const char* s)
{
	for(int i=0;s[i];i++)ans[ca++]=s[i];
}
int find(int x)
{
	for(int i=0;i<7;i++)if(b[i]==x)return i;
	return 0;
}
void hold()
{
	act("W");
	if(hld==7)hld=cur,cur=*(b++);
	else std::swap(hld,cur);
}
int l,r;
void bag1()
{
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==L)h=vis[S];
		else h=1;
		if(cur==L)c=vis[S];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		//cccc<<SS[hld]<<' '<<SS[cur]<<' '<<vis[S]<<' '<<h<<' '<<c<<endl;
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&cur==T)hold();
		switch(cur)
		{
			case I: case O: act("XrH");break;
			case S: case L: act("AlH");break;
			case Z: act("XRH");break;
			case J: act("XrLsAH");break;
			case T: if(vis[Z])act("ZsXH");else act("sLH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
	}
}
void bag2()
{
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==I)h=vis[S]&&(!vis[L]||vis[O]);
		else h=1;
		if(cur==I)c=vis[S]&&(!vis[L]||vis[O]);
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&cur==T)hold();
		switch(cur)
		{
			case J: act("XlH");break;
			case Z: act("lXRsLH");break;
			case O: act("RH");break;
			case S: act("rLZsRH");break;
			case L: act("ZrH");break;
			case T: act("ZsZH");break;
			case I: if(vis[L])act("AsrZH");else act("rXH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
	}
}
bool combo;
void ms31()
{
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==Z)h=vis[L];
		else h=1;
		if(cur==Z)c=vis[L];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&cur==T)hold();
		switch(cur)
		{
			case J: act("ZlH");break;
			case T: act("lRXH");break;
			case S: act("XsXH");break;
			case I: act("XrH");break;
			case O: act("rH");break;
			case L: act("RRXsRZH");break;
			case Z: act("rLZsRH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
	}
}
void ms32()
{
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==T)h=vis[L]&&vis[J]&&vis[S];
		else h=1;
		if(cur==T)c=vis[L]&&vis[J]&&vis[S];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&cur==T)hold();
		switch(cur)
		{
			case I: act("XlH");break;
			case Z: act("lXH");break;
			case J: if(vis[Z])act("LsXSSAH");else act("LZH");break;
			case S: act("XsXH");break;
			case T: act("XsZZH");break;
			case O: act("rH");break;
			case L: act("rLZH");break;
		}
		vis[cur]=1;
		combo=cur==T;
	}
}
void ms33()
{
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==Z)h=vis[L];
		else h=1;
		if(cur==Z)c=vis[L];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&((cur==T)^combo))hold();
		switch(cur)
		{
			case J: act("XlH");break;
			case L: act("LsLAH");break;
			case Z: act("lRXH");break;
			case T: act("XsXH");break;
			case S: act("RXsRH");break;
			case I: act("XrH");break;
			case O: act("rLH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
		combo=cur==T;
	}
}
void ms34()
{
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==T)h=vis[S];
		else h=1;
		if(cur==T)c=vis[S];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&((cur==T||(cur==S&&ooo!=T))^combo))hold();
		switch(cur)
		{
			case J: if(vis[L])act("rAH");else act("rXH");break;
			case L: if(vis[J])act("rAH");else act("rZH");break;
			case O: act("LsRRRH");break;
			case I: act("XlH");break;
			case Z: act("lXH");break;
			case T: act("XsXH");break;
			case S: act("XsXH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
		combo=cur==S;
	}
}
void ms35()
{
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==T)h=vis[S]&&(vis[J]||vis[L]);
		else h=1;
		if(cur==T)c=vis[S]&&(vis[J]||vis[L]);
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&((cur==T||(cur==S&&ooo!=T))^combo))hold();
		switch(cur)
		{
			case J: if(vis[L])act("lAH");else act("lXH");break;
			case L: if(vis[J])act("lAH");else act("lZH");break;
			case O: act("rLH");break;
			case I: act("XrH");break;
			case Z: act("RXH");break;
			case T: act("ZsZH");break;
			case S: act("ZsRsZH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
		combo=cur==S;
	}
}
void ms36()
{
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==T)h=vis[S]&&vis[O];
		else h=1;
		if(cur==T)c=vis[S]&&vis[O];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&((cur==T||(cur==S&&ooo!=T))^combo))hold();
		switch(cur)
		{
			case J: act("XlH");break;
			case Z: act("lXRsLH");break;
			case O: act("RH");break;
			case I: act("rZsRH");break;
			case L: act("ZrH");break;
			case S: act("XLsXH");break;
			case T: act("ZsZH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
		combo=cur==S;
	}
}
void up0()
{
	//cccc<<"UP0"<<endl;
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==I)h=vis[O];
		else h=1;
		if(cur==I)c=vis[O];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&((cur==T)^combo))hold();
		switch(cur)
		{
			case J: act("XlH");break;
			case Z: act("lXRsLH");break;
			case O: act("rH");break;
			case I: act("XrH");break;
			case S: act("XsRH");break;
			case L: act("rLZH");break;
			case T: act("ZsXH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
		combo=cur==T;
	}
}
void up1()
{
	return up0();
}
void up2()
{
	//cccc<<"UP2"<<endl;
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==O)h=vis[I];
		else h=1;
		if(cur==O)c=vis[I];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&((cur==T)^combo))hold();
		switch(cur)
		{
			case J: act("XlH");break;
			case Z: act("lXRsLH");break;
			case O: act("rH");break;
			case I: act("rXH");break;
			case S: act("XsRH");break;
			case L: act("ZRRRH");break;
			case T: act("ZsZH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
		combo=cur==T;
	}
}
void work1()
{
	//cccc<<"WORK1"<<endl;
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==L)h=vis[O];
		else h=1;
		if(cur==L)c=vis[O];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&((cur==T||(cur==S&&ooo!=T))^combo))hold();
		switch(cur)
		{
			case I: act("XrH");break;
			case J: act("XlH");break;
			case Z: act("lXRsLH");break;
			case O: act("RRH");break;
			case L: act("RRAH");break;
			case S: act("XLsXH");break;
			case T: act("ZsZH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
		combo=cur==S;
	}
}
void work2()
{
	//cccc<<"WORK2"<<endl;
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==L)h=vis[O];
		else h=1;
		if(cur==L)c=vis[O];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&((cur==T||(cur==S&&ooo!=T))^combo))hold();
		switch(cur)
		{
			case I: act("rXH");break;
			case J: act("XlH");break;
			case Z: act("lXRsLH");break;
			case O: act("RRH");break;
			case L: act("RRAH");break;
			case S: act("XLsXH");break;
			case T: act("ZsZH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
		combo=cur==S;
	}
}
void down1()
{
	//cccc<<"DOWN1"<<endl;
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==I)h=vis[J]||vis[L];
		else if(ooo==J||ooo==L)h=vis[J^L^ooo]==vis[I];
		else h=1;
		if(cur==I)c=vis[J]||vis[L];
		else if(cur==J||cur==L)c=vis[J^L^cur]==vis[I];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		//cccc<<SS[hld]<<' '<<SS[cur]<<' '<<h<<' '<<c<<' '<<vis[J]<<' '<<vis[L]<<endl;
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&((cur==T||(cur==S&&ooo!=T))^combo))hold();
		switch(cur)
		{
			case Z: act("lXH");break;
			case O: act("RRH");break;
			case J: if(vis[I])act("XrH");else act("ZrH");break;
			case L: if(vis[I])act("ZrH");else act("XrH");break;
			case I: if(vis[L])act("XrH");else act("rXH");break;
			case S: act("XLsXH");break;
			case T: act("ZsZH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
		combo=cur==S;
	}
}
void down2()
{
	//cccc<<"DOWN2"<<endl;
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==I)h=vis[O];
		else h=1;
		if(cur==I)c=vis[O];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&((cur==T||(cur==S&&ooo!=T))^combo))hold();
		switch(cur)
		{
			case J: act("XlH");break;
			case Z: act("LsRRsXH");break;
			case L: act("rLZH");break;
			case O: act("rH");break;
			case I: act("XrH");break;
			case S: act("XLsXH");break;
			case T: act("ZsZH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
		combo=cur==S;
	}
}
void down3()
{
	//cccc<<"DOWN3"<<endl;
	{
		cur=*(b++);int ooo=(hld==7?*b:hld);
		bool h=0,c=0;
		if(ooo==O)h=vis[I];
		else h=1;
		if(cur==O)c=vis[I];
		else c=1;
		h&=!vis[ooo],c&=!vis[cur];
		if(h==1&&c==0)hold();
		else if(h==1&&c==1&&((cur==T||(cur==S&&ooo!=T))^combo))hold();
		switch(cur)
		{
			case J: act("XlH");break;
			case Z: act("lXRsLH");break;
			case O: act("rH");break;
			case I: act("rXH");break;
			case L: act("RRAH");break;
			case S: act("XLsXH");break;
			case T: act("ZsZH");break;
		}
		vis[cur]=1;//cccc<<"ACT: "<<SS[cur]<<endl;
		combo=cur==S;
	}
}

typedef void(*nodes)(void);

nodes works[16]={bag1,bag2,ms31,ms32,ms33,ms34,ms35,ms36,up0,up1,up2,down1,down2,down3,work1,work2};
int noderuns[100]=
{
	-9,
	0,1,8,10,
	2,3,4,5,6,7,
	9,10,
	11,12,13,
	14,15
};
int nodenext[100]=
{
	-9,
	2,3,4,0,
	6,7,8,9,10,3,
	12,0,
	14,15,0,
	17,0
};

bool init(const Bot::Environment& environment)
{
	if(!availability(environment)) return false;
    if(Bot::QuickPlaySearch::enabled(environment))return true;
	node=-1;i=0;hld=7;
	for(int i=0;i<7;i++)
	{
		blocks[i]=BotPiece::definition(i,environment);
	}
	return 1;
}

int getpos(const Bot::Environment& environment,const Runtime::PlayerSessionState& player,Runtime::Playfield mp,std::vector<int>nex,const std::vector<int>& unseenBagPieces,int sx,int sy,int stype,int hold,int &ans_x,int &ans_y,int &ans_type,int &ans_rot)
{
    if(Bot::QuickPlaySearch::enabled(environment)) {
        const auto shape=[&](const std::vector<std::uint32_t>& rows,int width,int height,int,int) {
            Runtime::Playfield field;field.initialize(width,height);
            for(int y=-Runtime::kHiddenRowCapacity;y<height;++y)for(int x=0;x<width;++x)
                if(rows[height-1-y]&(std::uint32_t{1}<<x))field.set_cell(y,x,Runtime::BoardCell::piece(1));
            auto geometry=environment;geometry.fieldWidth=width;geometry.fieldHeight=height;
            return 512*Bot_candy0014::terrain(geometry,field);
        };
        // Fixed opener scripts cannot survive QP's altered bags/boards. Keep
        // Mech's perfect-clear objective through the shared rule search.
        const auto result=Bot::QuickPlaySearch::adapt(Bot::QuickPlaySearch::Profile::Mech,
            environment,player,mp,nex,unseenBagPieces,{{sx,sy},Runtime::Rotation::from_index(stype)},hold,{},shape);
        ans_x=result.pose.position.row;ans_y=result.pose.position.col;ans_type=result.pose.rotation.index();
        ans_rot=result.rotation.rotated()?result.rotation.kick().index()+1:0;return result.selected;
    }
	(void)unseenBagPieces; // Interface-only metadata; strategy intentionally unchanged.
	(void)player;
	(void)environment; // Used by debug-only assertions below in Release builds.
	for(int family=0;family<7;++family) blocks[family]=BotPiece::definition(family,environment);
	//position, full/mini/non spin, rot>last_kick+1:0
	b=a;
	ans_x=sx,ans_y=sy;
	ans_type=stype,ans_rot=0;
	ca=0;b[0]=nex[0];b[1]=nex[1];
	if(i==0)
	{
		for(int i=0;i<8;i++)vis[i]=0;
		if(node==-1)
		{
			//start
			int oo=6,ii=6;
			assert(nex.size()>=6);
			for(int i=0;i<6;i++)if(nex[i]==3)oo=i;else if(nex[i]==0)ii=i;
			if(oo<ii)
			{
				node=1;l=2,r=12;
			}
			else
			{
				node=5;l=2,r=13;
			}
		}
		else if(node==0)
		{
			if(l<=2&&r<=7)node=11,l+=1,r+=5;
			else if(r<=7)node=13,l-=1,r+=5;
			else node=16,r-=2;
		}
	}
	int cur=nex[0];int ooo=(hld==7?nex[1]:hld);
	works[noderuns[node]]();
	i++;
	if(i==7)i=0,node=nodenext[node];
	ans[ca]=0;
	std::string result=ans;
	if(ans[0]=='W')std::swap(cur,ooo);
	Bot_Place(mp,blocks[cur],result,ans_x,ans_y,ans_type,ans_rot);
	if(ans[0]=='W'){
		assert(environment.holdEnabled&&hold!=-2);
		if(hold==-1) return nex[1];
		return hold;
	}
	return nex[0];
}

}
