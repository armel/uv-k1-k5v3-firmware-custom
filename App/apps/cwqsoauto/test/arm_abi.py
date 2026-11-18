"""Execute the actual linked Thumb-1 leaf bytes up to resident API dispatch.
Only eight supported Thumb encodings are needed. Unknown opcodes fail closed.
Checks arguments, tail target, callee-saved registers, LR and SP.
"""
from support import app,cc,p
import subprocess,re,json,struct
raw=(app/'cwqsoauto.bin').read_bytes();base=0x20000280
nm=subprocess.run([str(cc.with_name(cc.name.replace('gcc','nm'))),'-S',str(app/'cwqsoauto.elf')],capture_output=True,text=True,check=True).stdout
symbols={m[4]:(int(m[1],16),int(m[2],16)) for line in nm.splitlines() if (m:=re.match(r'([0-9a-f]+) ([0-9a-f]+) (\w) (\S+)',line))}
dis=subprocess.run([str(cc.with_name(cc.name.replace('gcc','objdump'))),'-d',str(app/'cwqsoauto.elf')],capture_output=True,text=True,check=True).stdout
(p/'thumb-disassembly.txt').write_text(dis)
g=symbols['g'][0];api=0x10000000;checks=0
def run(name,args,wpm,target,expected,readValue=0):
 global checks
 memory={base+i:b for i,b in enumerate(raw)}
 def put(addr,value):
  for i in range(4):memory[addr+i]=(value>>(8*i))&255
 def word(addr):return sum(memory.get(addr+i,0)<<(8*i) for i in range(4))
 put(g+68,api);memory[g+1]=wpm
 slots={344:0x08000101,264:0x08000201,80:0x08000301,272:0x08000401,76:0x08000501,60:0x08000601}
 for off,fn in slots.items():put(api+off,fn)
 r=[0x12340000+i for i in range(16)];r[:len(args)]=args;r[13]=0x20008000;r[14]=0x08005555
 saved=r.copy();pc=symbols[name][0]
 for step in range(16):
  op=memory[pc]|memory[pc+1]<<8;at=pc;pc+=2
  if op&0xF800==0x4800:r[(op>>8)&7]=word(((at+4)&~3)+(op&255)*4)
  elif op&0xF800==0x6800:r[op&7]=word(r[(op>>3)&7]+((op>>6)&31)*4)
  elif op&0xF800==0x7800:r[op&7]=memory[r[(op>>3)&7]+((op>>6)&31)]
  elif op&0xF800==0x3000:r[(op>>8)&7]=(r[(op>>8)&7]+(op&255))&0xffffffff
  elif op&0xF800==0x2000:r[(op>>8)&7]=op&255
  elif op&0xF800==0:r[op&7]=(r[(op>>3)&7]<<((op>>6)&31))&0xffffffff
  elif op&0xFFC0==0x4300:r[op&7]|=r[(op>>3)&7]
  elif op&0xFFC0==0x4380:r[op&7]&=(~r[(op>>3)&7])&0xffffffff
  elif op&0xFF00==0x4600:r[(op&7)|((op>>4)&8)]=r[(op>>3)&15]
  elif op&0xFE00==0xB400:
   regs=[i for i in range(8) if op&(1<<i)]+([14] if op&256 else [])
   r[13]-=4*len(regs)
   for i,reg in enumerate(regs):put(r[13]+4*i,r[reg])
  elif op&0xFF87==0x4780:
   assert name=='gainBase' and r[(op>>3)&15]==slots[76] and r[0]==72 and r[13]%8==0
   r[14]=pc|1;r[0]=readValue
   r[1]=r[2]=r[3]=r[12]=0xA5A5A5A5
  elif op&0xFE00==0xBC00:
   regs=[i for i in range(8) if op&(1<<i)]+([15] if op&256 else [])
   for i,reg in enumerate(regs):r[reg]=word(r[13]+4*i)
   r[13]+=4*len(regs)
   assert name=='gainBase' and r[15]==saved[14] and r[0]==expected[0]
   assert r[4:12]==saved[4:12] and r[13]==saved[13]
   checks+=1;return
  elif op&0xFF87==0x4700:
   assert r[(op>>3)&15]==slots[target],(name,'dispatch',r)
   assert r[:len(expected)]==expected,(name,'arguments',r,expected)
   assert r[4:12]==saved[4:12] and r[13:15]==saved[13:15],(name,'ABI')
   if name=='dotMs':assert 1200//r[1]<=240 # resident low return word fits uint16_t
   checks+=1;return
  else:raise AssertionError((name,hex(at),hex(op)))
 raise AssertionError('Missing tail dispatch')
for value in [0,1,64,511,512,1599,2281]:
 run('readAsset',[value,0x20004000,64],18,264,[value,0x20004000,64])
for gain in range(16):
 for basegain in [0,0x100,0xB3A0,0xFFF0]:run('setAudioGain',[basegain,gain],18,80,[72,basegain|gain])
for speed in range(5,61):run('dotMs',[],speed,272,[1200,speed])
run('nowCycles',[],18,344,[])
for x in [2,30,110]:
 for line in range(7):run('normalText',[0x20004000,x,line],18,60,[0x20004000,x,0,line])
for value in [0,1,15,16,255,0xB3AF,0xFFF0,0xFFFF]:run('gainBase',[],18,76,[value&0xFFF0],value)
result=dict(arm_thumb_abi_checks=checks,passed=True,routines={n:symbols[n][1] for n in ['nowCycles','readAsset','setAudioGain','dotMs','normalText','gainBase']})
(p/'arm-abi.json').write_text(json.dumps(result,indent=2));print(result)
