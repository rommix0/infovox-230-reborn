"""Disassembler for Infovox RULSYS rule bytecode in .ivx sections (work in progress)."""
import struct,sys,collections
from ivx import Ivx,PNAMES
# operand size in bytes (after the opcode word); 'var' = self-sized pattern; 'rep' = replace
SZ={0x00:0,0x04:0,0x10:2,0xb0:0,0xc8:2,0xcc:0,0xd0:0,0xd4:0,
    0x40:2,0x44:2,0x48:2,0x4c:2,0x50:2,0x7c:2,0x80:2,
    0x54:8,0x58:8,0x5c:8,0x60:8,0x64:8,0x90:8,0x94:8,0xc0:8,0xc4:8,
    0x68:'var',0x6c:'cvar',0x9c:'cvar',0xb4:2,0xb8:'rep',0xbc:'rep',
    0x100:'pv',0x104:'pv',0x108:'pv',0x10c:'pv',
    0x150:2,0x154:0,0x158:0,0x15c:2,0x160:0,0x164:2,0x168:0,0x16c:0,0x170:2,0x174:0,0x178:2,
    0x17c:0,0x180:2,0x184:0,0x188:4,0x18c:2,0x190:2,0x194:2,0x198:0,0x19c:0,0x1a0:0,0x1a4:0,
    0x1a8:0,0x1ac:0,0x1b0:0,0x1b4:0,0x1b8:0,0x1bc:0,0x1c0:0,0x1c4:0,0x1c8:2,0x1cc:2,0x1f0:2,0x1f4:2,0x1f8:2}
def rep_split(w):
    """replace operand = del*256 + 12*ins (ins*12 may carry into the high byte)"""
    for d in range(w>>8,-1,-1):
        r=w-d*256
        if r%12==0: return d,r//12
    return w>>8,(w&0xff)//12
def opsize(b,p,op):
    s=SZ.get(op)
    if s is None: return None
    if s=='var': return struct.unpack_from('>H',b,p+2)[0]&0x0ffe
    if s=='pv': return 2 if (b[p+3]&0x80) else 4
    if s=='cvar': return 2+(struct.unpack_from('>H',b,p+4)[0]&0x0ffe)
    if s=='rep':
        w=struct.unpack_from('>H',b,p+2)[0]; return 2+2*rep_split(w)[1]
    return s
def rules(b,start,end):
    """yield (pos, op, len, bodystart, bodyend) for each rule header in a module"""
    p=start+2
    while p<end:
        op=struct.unpack_from('>H',b,p)[0]
        if op in (0x08,0x0c):
            L=struct.unpack_from('>H',b,p+2)[0]; yield p,op,L; p=p+2+L
        else:
            yield p,op,None; p+=2
            if op==0: return
def check(x,sec):
    b=x.b; start=x.H[sec]; 
    ends=sorted(set(list(x.H[3:7])+list(x.H[8:15])+[len(b)]))
    end=min(e for e in ends if e>start)
    bad=collections.Counter(); ok=0; n=0; other=collections.Counter()
    for p,op,L in rules(b,start,end):
        if L is None: other[op]+=1; continue
        n+=1; q=p+4; e=p+2+L; last=None
        while q<e:
            o=struct.unpack_from('>H',b,q)[0]; s=opsize(b,q,o)
            if s is None: bad['unk%x'%o]+=1; break
            last=o; q+=2+s
        if q==e: ok+=1
        elif q>e: bad['over_after_%x'%last]+=1
    return n,ok,bad,other
if __name__=="__main__" and len(sys.argv)==1:
    import glob
    for f in sorted(glob.glob('../bin/*.ivx')):
        x=Ivx(f)
        for sec in (5,6,9,10,11,12):
            if x.H[sec]==0: continue
            n,ok,bad,other=check(x,sec)
            print('%-12s H[%2d] rules=%4d ok=%4d nonrule=%s bad=%s'%(f[7:],sec,n,ok,dict(other),dict(bad.most_common(6))))

# ---------------------------------------------------------------- pretty listing
MOD={9:'M0',10:'M1',12:'M2',5:'M3',6:'M4',11:'M5'}   # run_module index -> section
MN={0x00:'END',0x04:'BEGIN',0x08:'RULE',0x0c:'RULE*',0x10:'NEXT',
 0x40:'SEEK',0x44:'FOCUS',0x48:'SCAN',0x4c:'R',0x50:'R?',0x7c:'L',0x80:'L?',
 0x54:'SEEKF',0x58:'FOCUSF',0x5c:'SCANF',0x60:'RF',0x64:'RF?',0x90:'LF',0x94:'LF?',
 0x68:'FOCUSP',0x6c:'RP',0x9c:'LP',
 0xb0:'FNEXT',0xb4:'UNWRAP',0xb8:'REPL+',0xbc:'REPL',0xc0:'SETF+',0xc4:'SETF',0xc8:'GEMIN',
 0xcc:'CPREV',0xd0:'CHEAD',0xd4:'CTAIL',
 0x100:'LD',0x104:'STs',0x108:'STu',0x10c:'ST',
 0x150:'A=',0x154:'A=B',0x158:'A=B=-B',0x15c:'A+=',0x160:'A+=B',0x164:'A-=',0x168:'A-=B',0x16c:'A=|A|',
 0x170:'A*=',0x174:'A*=B',0x178:'A/=',0x17c:'A/=B',0x180:'A/=',0x184:'A/=B',0x188:'A*=frac',
 0x18c:'A>>=',0x190:'A>>=',0x194:'A<<=',0x198:'PUSHQ1',0x19c:'PUSHQ2',0x1a0:'B=POPQ1',0x1a4:'B=POPQ2',
 0x1a8:'B=R0',0x1ac:'B=R1',0x1b0:'B=R2',0x1b4:'B=R3',0x1b8:'R0=A',0x1bc:'R1=A',0x1c0:'R2=A',0x1c4:'R3=A',
 0x1c8:'B=V',0x1cc:'V=A',0x1f0:'OP1F0',0x1f4:'OP1F4',0x1f8:'OP1F8'}
class Lister:
    def __init__(s,x):
        s.x=x; s.ph={}
        from ivx import parse_phoneme
        for p,r in x.chain()[1:]:
            s.ph[p-x.H[8]]=parse_phoneme(r)[0].strip() or '_'
    def phn(s,i): return s.ph.get(i,'#%x'%i)
    def par(s,w,tw=None):
        hi,lo=w>>8,w&0xff
        if lo&0x80: return 'v%02x'%lo
        n=PNAMES[lo] if lo<len(PNAMES) else 'p%d'%lo
        return '%s@%04x'%(n,tw) if tw is not None else n
    def feat(s,v,m): return '[%08x/%08x]'%(v,m)
    def pattern(s,b,p):
        h=struct.unpack_from('>H',b,p)[0]; q=p+2; parts=[]
        if h&0x8000: parts.append(s.phn(struct.unpack_from('>H',b,q)[0])); q+=2
        if h&0x4000: v,m=struct.unpack_from('>II',b,q); parts.append(s.feat(v,m)); q+=8
        if h&0x2000: parts.append('params:'+b[q:p+(h&0xffe)].hex())
        return '{'+' '.join(parts)+'}'
    def insn(s,b,p):
        op=struct.unpack_from('>H',b,p)[0]; sz=opsize(b,p,op); m=MN.get(op,'op%x'%op); a=p+2
        W=lambda o=0: struct.unpack_from('>H',b,a+o)[0]
        S=lambda o=0: struct.unpack_from('>h',b,a+o)[0]
        if op in (0x40,0x44,0x48,0x4c,0x50,0x7c,0x80): arg=s.phn(W())
        elif op in (0x54,0x58,0x5c,0x60,0x64,0x90,0x94,0xc0,0xc4): arg=s.feat(*struct.unpack_from('>II',b,a))
        elif op==0x68: arg=s.pattern(b,a)
        elif op in (0x6c,0x9c): c=W(); arg='{%d,%d}'%(c>>8,c&0xff)+s.pattern(b,a+2)
        elif op in (0xb8,0xbc):
            d,n=rep_split(W()); arg='del %d -> '%d+' '.join(s.phn(W(2+2*i)) for i in range(n))
        elif op in (0x100,0x104,0x108,0x10c): arg=s.par(W(),W(2) if sz==4 else None)
        elif op==0x188: arg='%d/2^%d (=%.3f)'%(S(),S(2),S()/2**S(2))
        elif op in (0x10,0xc8): arg='#%d'%W()
        elif sz==2: arg=str(S())
        else: arg=''
        return op,sz,('%-7s %s'%(m,arg)).rstrip()
    def module(s,sec,out):
        b=s.x.b; start=s.x.H[sec]
        secs=sorted(set(list(s.x.H[3:7])+list(s.x.H[8:15])+[len(b)])); end=min(e for e in secs if e>start)
        out.write('; ===== module %s (H[%d]) @%x..%x lead word %04x\n'%(MOD[sec],sec,start,end,struct.unpack_from('>H',b,start)[0]))
        for p,op,L in rules(b,start,end):
            if L is None: out.write('%6x  %s\n'%(p,MN.get(op,'op%x'%op))); continue
            body=[]; q=p+4; e=p+2+L
            while q<e:
                o,sz,t=s.insn(b,q); body.append(t); q+=2+sz
            out.write('%6x  %s  %s\n'%(p,MN[op],' ; '.join(body)))
if __name__=='__main__' and len(sys.argv)>1:
    x=Ivx(sys.argv[1]); L=Lister(x)
    secs=[int(a) for a in sys.argv[2:]] or [9,10,12,5,6,11]
    for sec in secs:
        if x.H[sec]: L.module(sec,sys.stdout)
