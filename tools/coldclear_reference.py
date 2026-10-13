#!/usr/bin/env python3
"""Generate terrain goldens with the downloaded, unmodified Rust Standard eval.

Only fixture regeneration needs rustc. The game and C++ tests do not use Rust.
The small board below supplies the upstream evaluator's interface; the actual
feature extraction, slot detection and scoring come from standard.rs verbatim.
"""
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WRAPPER = r'''
#![allow(dead_code, unused_imports)]
use std::ops::{Add, Div, Mul};
#[derive(Clone,Copy,PartialEq)] enum Piece { I,T }
#[derive(Clone,Copy)] enum RotationState { North,East,South,West }
#[derive(Clone,Copy)] struct PieceState(Piece,RotationState);
#[derive(Clone,Copy)] enum TspinStatus { None,Full }
#[derive(Clone,Copy)] struct FallingPiece { kind:PieceState,x:i32,y:i32,tspin:TspinStatus }
impl FallingPiece {
 fn cells(self)->[(i32,i32);4] {
  let mut cells=[(-1,0),(0,0),(1,0),(0,1)];
  let count=match self.kind.1 { RotationState::North=>0,RotationState::East=>1,RotationState::South=>2,RotationState::West=>3 };
  for (x,y) in &mut cells { for _ in 0..count { let xx=*x;*x=*y;*y=-xx; } *x+=self.x;*y+=self.y; }
  cells
 }
 fn sonic_drop(&mut self,b:&Board) {
  let amount=self.cells().iter().map(|&(x,y)|y-b.heights[x as usize]).min().unwrap();
  if amount>0 {self.y-=amount;} else if amount<0 { loop { self.y-=1; if self.cells().iter().any(|&(x,y)|b.occupied(x,y)) {self.y+=1;break;} } }
 }
}
#[derive(Clone)] struct Bag(u32);
impl Bag { fn contains(&self,_:Piece)->bool {self.0&32!=0} fn len(&self)->usize {self.0.count_ones() as usize} }
#[derive(Clone)] struct Board { rows:[u16;40],heights:[i32;10],b2b_bonus:bool,hold_piece:Option<Piece>,bag:Bag }
impl Board {
 fn column_heights(&self)->&[i32;10] {&self.heights}
 fn occupied(&self,x:i32,y:i32)->bool { x<0||x>=10||y<0||y>=40||(self.rows[y as usize]&(1<<x)!=0) }
 fn get_row(&self,y:i32)->&u16 {&self.rows[y as usize]}
 fn next_bag(&self)->Bag {self.bag.clone()}
 fn on_stack(&self,p:&FallingPiece)->bool {p.cells().iter().any(|&(x,y)|self.occupied(x,y-1))}
 fn refresh(&mut self) {self.heights=[0;10];for y in 0..40 {for x in 0..10 {if self.rows[y]&(1<<x)!=0 {self.heights[x]=y as i32+1;}}}}
 fn lock_piece(&mut self,p:FallingPiece)->LockResult {
  for (x,y) in p.cells() {self.rows[y as usize]|=1<<x;}
  let lines=self.rows.iter().filter(|&&r|r==1023).count();
  let mut next=[0;40];let mut dest=0;for &row in &self.rows {if row!=1023 {next[dest]=row;dest+=1;}}
  self.rows=next;self.refresh();
  let kind=match lines {0=>PlacementKind::Tspin,1=>PlacementKind::Tspin1,2=>PlacementKind::Tspin2,3=>PlacementKind::Tspin3,_=>panic!("invalid cutout")};
  LockResult {perfect_clear:false,b2b:false,combo:None,placement_kind:kind,garbage_sent:0}
 }
}
#[derive(Clone,Copy)] enum PlacementKind { None,Clear1,Clear2,Clear3,Clear4,Tspin,Tspin1,Tspin2,Tspin3,MiniTspin,MiniTspin1,MiniTspin2 }
impl PlacementKind {fn is_clear(self)->bool {!matches!(self,Self::None|Self::Tspin|Self::MiniTspin)}}
struct LockResult {perfect_clear:bool,b2b:bool,combo:Option<u32>,placement_kind:PlacementKind,garbage_sent:u32}
struct MoveCandidate<V> {board:Board,lock:LockResult,evaluation:V}
trait Evaluator {type Value;type Reward;fn name(&self)->String;fn pick_move(&self,c:Vec<MoveCandidate<Self::Value>>,incoming:u32)->MoveCandidate<Self::Value>;fn evaluate(&self,lock:&LockResult,b:&Board,time:u32,p:Piece)->(Self::Value,Self::Reward);}
trait Evaluation<R>:Eq+Ord+Default+Clone+Add<R,Output=Self>+Add<Output=Self>+Div<usize,Output=Self>+Mul<usize,Output=Self> {fn modify_death(self)->Self;fn weight(self,min:&Self,rank:usize)->i64;fn improve(&mut self,other:Self);}
mod libtetris {pub const COMBO_GARBAGE:[u32;12]=[0,0,1,1,2,2,3,3,4,4,4,5];}
'''
MAIN = r'''
fn main() {
 use std::io::{self,BufRead};
 for line in io::stdin().lock().lines() {
  let line=line.unwrap(); let words:Vec<_>=line.split_whitespace().collect();
  let b2b=words[0]=="1";let count:usize=words[1].parse().unwrap();
  let mut b=Board {rows:[0;40],heights:[0;10],b2b_bonus:b2b,hold_piece:None,bag:Bag(15)};
  if count==1 {b.bag=Bag(63);} else if count==2 {b.bag=Bag(32);} else if count==3 {b.bag=Bag(32);b.hold_piece=Some(Piece::T);}
  for (i,r) in words[2..].iter().enumerate() {b.rows[i]=r.parse().unwrap();} b.refresh();
  let lock=LockResult {perfect_clear:false,b2b:false,combo:None,placement_kind:PlacementKind::None,garbage_sent:0};
  println!("{}",Standard::default().evaluate(&lock,&b,0,Piece::I).0.value);
 }
}
'''

def main():
    import random
    rng = random.Random(279)
    boards = [[], [15], [1022]*4, [1023 ^ 16]*8,
              [887, 887, 951], [1022, 1016, 989], [882, 766, 757, 628]]
    for _ in range(80):
        rows = [0] * 18
        for x in range(10):
            for y in range(rng.randrange(1,18)):
                if rng.random() > .16:
                    rows[y] |= 1 << x
        boards.append([r if r != 1023 else r ^ 1 for r in rows])
    cases = [(rows, b2b, t) for rows in boards for b2b in (0,1) for t in range(4)]
    source = (ROOT/'third_party/cold-clear/bot/src/evaluation/standard.rs').read_text()
    source = re.sub(r'^use .*;\n', '', source, flags=re.M)
    source = re.sub(r'^#\[serde.*\]\n', '', source, flags=re.M)
    source = source.replace(', Serialize, Deserialize','')
    with tempfile.TemporaryDirectory(prefix='coldclear-reference-') as tmp:
        path = Path(tmp)
        (path/'reference.rs').write_text(WRAPPER+source+MAIN)
        subprocess.run(['rustc','--edition=2018','-O',str(path/'reference.rs'),'-o',str(path/'reference')],check=True)
        inputs = '\n'.join(f'{b} {t} '+ ' '.join(map(str, rows)) for rows,b,t in cases)+'\n'
        output = subprocess.run([str(path/'reference')],input=inputs,text=True,capture_output=True,check=True)
    scores = list(map(int,output.stdout.split()))
    assert len(scores) == len(cases)
    fixture = ROOT/'tests/fixtures/coldclear_terrain.csv'
    fixture.parent.mkdir(parents=True,exist_ok=True)
    fixture.write_text('# Upstream 279edd7c3177ff8077f6a930193397814b281f27 Standard terrain\n'+
        '\n'.join(f'{b},{t},{score},'+ ';'.join(map(str,rows)) for (rows,b,t),score in zip(cases,scores))+'\n')
    print(f'Generated {len(scores)} upstream terrain fixtures in {fixture}')

if __name__ == '__main__':
    main()
