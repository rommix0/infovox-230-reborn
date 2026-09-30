import struct,sys
from ivx import Ivx
from ivxdasm import rules,opsize
x=Ivx(sys.argv[1]); b=x.b; sec=int(sys.argv[2]); lim=int(sys.argv[3]) if len(sys.argv)>3 else 10
secs=sorted(set(list(x.H[3:7])+list(x.H[8:15])+[len(b)])); end=min(e for e in secs if e>x.H[sec])
k=0
for p,op,L in rules(b,x.H[sec],end):
    if L is None: continue
    q=p+4; e=p+2+L; tr=[]
    while q<e:
        o=struct.unpack_from('>H',b,q)[0]; s=opsize(b,q,o)
        if s is None: break
        tr.append('%x'%o); q+=2+s
    if q!=e:
        ws=struct.unpack_from('>%dH'%((L+2)//2),b,p)
        print(hex(p),' '.join('%04x'%w for w in ws)); print('   ops:',' '.join(tr),'| stuck at +%x'%(q-p)); k+=1
        if k>=lim: break
