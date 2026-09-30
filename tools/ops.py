import os,re,struct,pefile,json
L=[l for l in open('full.asm') if re.match(r'^[0-9a-f]{8}:',l)]
ins={}; order=[]
for l in L:
    a=int(l[:8],16); m=l[10:].split(None,1); ins[a]=(m[0],m[1].split(' ;')[0].strip() if len(m)>1 else ''); order.append(a)
nxt={order[i]:order[i+1] for i in range(len(order)-1)}
pe=pefile.PE(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','bin','Ivx230nt.dll'));img=pe.get_memory_mapped_image()
def walk(op,pc=0x1002024d):
    flags=None; eax=op; steps=0
    while steps<200:
        steps+=1; m,o=ins[pc]
        if m=='cmp' and o.startswith('eax,'):
            v=int(o.split(',')[1],0); flags=(eax>v)-(eax<v); pc=nxt[pc]; continue
        if m=='test': flags=(eax!=0); flags=0 if eax==0 else 1; pc=nxt[pc]; continue
        if o.startswith('dword ptr [edx*4'):
            i=img[0x206f8+eax]; t=struct.unpack_from('<I',img,0x20658+i*4)[0]; pc=t; continue
        if m in('jg','je','jne','ja','jmp','jl','jle','jge'):
            t=int(o,16); c={'jg':flags==1,'ja':flags==1,'je':flags==0,'jne':flags!=0,'jmp':True,'jl':flags==-1,'jle':flags!=1,'jge':flags!=-1}[m]
            pc=t if c else nxt[pc]; continue
        if m=='add' and o.startswith('eax,'):
            v=int(o.split(',')[1],0)&0xffffffff; eax=(eax+v)&0xffffffff; pc=nxt[pc]; continue
        if m=='xor' or (m=='mov' and o.startswith('dl')): pc=nxt[pc]; continue
        if m=='jmp' : pass
        if m=='call' and o.startswith('0x'): return int(o,16)
        if o.startswith('dword ptr [edx*4'):
            i=img[0x206f8+eax]; t=struct.unpack_from('<I',img,0x20658+i*4)[0]; pc=t; continue
        if m=='push' or m=='mov' and 'ecx' in o: pc=nxt[pc]; continue
        return ('stop',hex(pc),m,o)
    return None
res={}
for op in range(0,0x200,4):
    r=walk(op)
    res[op]=r
    print(hex(op), hex(r) if isinstance(r,int) else r)
