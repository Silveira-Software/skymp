from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_X86,UC_MODE_64
from unicorn.x86_const import *
import argparse
parser=argparse.ArgumentParser(description="Emulate captured AE instructions; no live engine calls or game-server access")
parser.add_argument('--text',type=Path,required=True,help='Private capture of AE .text starting at RVA 1000')
parser.add_argument('--catalog',type=Path,required=True,help='Existing hf-context-result.json, never a fresh catalog audit')
parser.add_argument('--header',type=Path,required=True)
parser.add_argument('--report',type=Path,required=True)
args=parser.parse_args()
text=args.text.read_bytes();image=0x140000000
assert hashlib.sha256(text[0x3c4140:0x3c44b7]).hexdigest()=='90ffdf94b996d3c038cc4d11965dbbf5399663142b87968fb85255c79422a779'
assert hashlib.sha256(text[0x1d6cf0:0x1d6d6a]).hexdigest()=='1a33c10d0ffb8055f24dc1430560bc28043e2b929a67de14eef12366fa3b6b77'
assert hashlib.sha256(text[0x3be890:0x3be918]).hexdigest()=='a8718c42108ad3f3e2cc8647cb2a3dfa31b5ed15eedd09e0da1877f77dfa2332'
start=image+0x3c5140;end=image+0x3c54b7
def allocate(level,weights,base=(100,50,50),budget=10):
 u=Uc(UC_ARCH_X86,UC_MODE_64);u.mem_map(image+0x3c5000,0x1000)
 u.mem_write(image+0x3c5000,text[0x3c4000:0x3c5000])
 u.mem_map(image+0x1ff9000,0x1000)
 # iAVDSkillMax only limits skills; the HMS paths never use this cap.
 u.mem_write(image+0x1ff964c,struct.pack('<I',100))
 arena=0x100000;u.mem_map(arena,0x20000);npc=arena+0x1000;head=arena+0x2000;stack=arena+0x10000
 for i,v in enumerate(base):u.mem_write(npc+0x1b4+2*i,struct.pack('<h',v))
 # Engine inserts a new node before equal weights (3bf992/3bfa48).
 order=sorted(range(3),key=lambda i:(-weights[i],-i))
 nodes=[arena+0x3000+0x20*i for i in range(3)]
 u.mem_write(head,struct.pack('<Q',nodes[0]))
 for n,i in enumerate(order):u.mem_write(nodes[n],struct.pack('<iiQQ',24+i,weights[i],nodes[n-1] if n else 0,nodes[n+1] if n<2 else 0))
 u.mem_write(stack,struct.pack('<Q',end));u.mem_write(stack+0x28,struct.pack('<I',sum(weights)));u.mem_write(stack+0x30,struct.pack('<I',budget))
 for reg,value in [(UC_X86_REG_RCX,npc),(UC_X86_REG_RDX,level-1),(UC_X86_REG_R8,head),(UC_X86_REG_R9,3),(UC_X86_REG_RSP,stack)]:u.reg_write(reg,value)
 u.emu_start(start,end,count=200000)
 return list(struct.unpack('<hhh',u.mem_read(npc+0x1b4,6)))
def resolve(data,pcLevel):
 u=Uc(UC_ARCH_X86,UC_MODE_64);code=image+0x1d7000
 u.mem_map(code,0x1000);u.mem_write(code,text[0x1d6000:0x1d7000])
 u.mem_map(image+0x3187000,0x1000);u.mem_map(image+0x1ad2000,0x1000)
 u.mem_write(image+0x1ad280c,struct.pack('<f',.001))
 arena=0x100000;u.mem_map(arena,0x10000)
 u.mem_write(image+0x31874f8,struct.pack('<Q',arena+0x3000))
 u.mem_write(arena+0x3040,struct.pack('<Q',arena+0x4000))
 u.mem_write(arena+0x4040,struct.pack('<H',pcLevel))
 npc=arena+0x1000
 u.mem_write(npc+8,struct.pack('<I',0x80 if data['pcLevelMultiplier'] else 0))
 u.mem_write(npc+0x10,struct.pack('<HHH',data['declaredLevelOrMultiplier'],data['calcMin'],data['calcMax']))
 stack=arena+0x8000;end=image+0x1d7d6a
 u.mem_write(stack,struct.pack('<Q',end));u.reg_write(UC_X86_REG_RSP,stack);u.reg_write(UC_X86_REG_RCX,npc)
 u.emu_start(image+0x1d7cf0,end,count=1000)
 return u.reg_read(UC_X86_REG_RAX)
def initialize(level,race,offsets,bonus):
 u=Uc(UC_ARCH_X86,UC_MODE_64);code=image+0x3bf000
 u.mem_map(code,0x1000);u.mem_write(code,text[0x3be000:0x3bf000])
 u.mem_map(image+0x2005000,0x1000);u.mem_write(image+0x2005b98,struct.pack('<f',bonus))
 arena=0x100000;u.mem_map(arena,0x10000);npc=arena+0x1000;racePtr=arena+0x3000
 u.mem_write(npc+0x14,struct.pack('<I',123));u.mem_write(npc+0x4c,struct.pack('<h',offsets[0]));u.mem_write(npc+0x3c,struct.pack('<hh',*offsets[1:]))
 u.mem_write(racePtr+0x10c,struct.pack('<fff',*race))
 for r,v in [(UC_X86_REG_RBP,npc),(UC_X86_REG_RSI,racePtr),(UC_X86_REG_R13,level-1),(UC_X86_REG_RSP,arena+0x8000)]:u.reg_write(r,v)
 u.emu_start(image+0x3bf890,image+0x3bf918,count=1000)
 return list(struct.unpack('<hhh',u.mem_read(npc+0x1b4,6)))
if __name__=='__main__':
 cases=[]
 for weights in [(1,1,1),(3,0,3),(2,3,0),(3,0,2),(1,4,0),(2,1,2),(0,0,0),(0,1,0),(0,0,1),(255,254,253)]:
  for level in [1,2,3,7,12,28,80,100]:
   for budget in [7,10]:cases.append({'level':level,'weights':weights,'budget':budget,'growth': [a-b for a,b in zip(allocate(level,weights,budget=budget),(100,50,50))]})
 context=json.loads(args.catalog.read_bytes())
 configs={}
 for actor in context['actors']:
  record=actor['templateContexts']['stats'].get('record') or {};data=record.get('acbs')
  if data:configs[record['reference']]=data
 levels=[]
 for source,data in configs.items():
  for pcLevel in [1,3,10,80]:levels.append({'source':source,**data,'pcLevel':pcLevel,'expected':resolve(data,pcLevel)})
 initial=[]
 for level in [1,2,3,12]:
  for race,offsets,bonus in [((100.75,50.9,50.5),(1,-50,0),5.9),((.75,50,50),(-1,0,0),5.5),((50.9,70.2,100.99),(-30,3,-50),5.0)]:
   initial.append({'level':level,'race':race,'offsets':offsets,'bonus':bonus,'expected':initialize(level,race,offsets,bonus)})
 report={'kind':'AE_1170_CAPTURED_INSTRUCTIONS_UNICORN_NOT_HUMAN_RUNTIME','exeSha256':'c434208894f07f604b852f29b8edc3a58c4de63de783373733e72b2b73f33be9','helperRva':'0x3c5140','helperSha256':hashlib.sha256(text[0x3c4140:0x3c44b7]).hexdigest(),'levelHelperSha256':hashlib.sha256(text[0x1d6cf0:0x1d6d6a]).hexdigest(),'cases':cases,'levelCases':levels,'catalogStatsSources':len(configs)}
 report['initialCases']=initial
 args.report.write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k not in ['cases','levelCases','initialCases']}), 'vectors',len(cases),'levelVectors',len(levels),'initialVectors',len(initial))
 header='// Generated from captured AE 1.6.1170 instructions in Unicorn; not a live Skyrim test.\n'
 header+='// Distribution SHA256 '+report['helperSha256']+'\n// GetLevel SHA256 '+report['levelHelperSha256']+'\n#pragma once\n#include <array>\nnamespace OriginalEngineFixtures {\n'
 header+='struct Allocation { uint16_t level; std::array<uint8_t,3> weights; uint32_t budget; std::array<int,3> growth; };\nconstexpr Allocation allocations[] = {\n'
 for c in cases:header+='  {'+str(c['level'])+', {'+','.join(map(str,c['weights']))+'}, '+str(c['budget'])+', {'+','.join(map(str,c['growth']))+'}},\n'
 header+='};\nstruct Level { uint16_t value, minimum, maximum, pcLevel, expected; bool pc; };\nconstexpr Level levels[] = {\n'
 for c in levels:header+='  {'+','.join(str(c[k])for k in ['declaredLevelOrMultiplier','calcMin','calcMax','pcLevel','expected'])+','+str(c['pcLevelMultiplier']).lower()+'}, // '+c['source']+'\n'
 header+='};\nstruct Initial { uint16_t level; std::array<float,3> race; std::array<int16_t,3> offsets; float bonus; std::array<int,3> expected; };\nconstexpr Initial initials[] = {\n'
 for c in initial:header+='  {'+str(c['level'])+', {'+','.join(str(float(v))+'f' for v in c['race'])+'}, {'+','.join(map(str,c['offsets']))+'}, '+str(float(c['bonus']))+'f, {'+','.join(map(str,c['expected']))+'}},\n'
 header+='};\n}\n';args.header.write_text(header)
