"""Infovox .ivx rule/lexicon file parser (work in progress)."""
import struct,sys,os
PNAMES=['DR','ST','FS','CS','FD','F1','B1','F2','B2','F3','B3','F4','B4','FH','K1','C1','K2','C2','FN','BN','AK','AC','AH','AN','A0','F0','XX','YY','ZZ']
class Ivx:
    def __init__(s,path):
        d=open(path,'rb').read(); s.hdr=d[:0x200]; s.b=d[0x200:]
        s.H=struct.unpack('>16I',s.b[:64])
        s.version=s.hdr[0x80:0xc0].split(b'\0')[0].decode('latin1')
        s.tag=s.b[0x40:0x4a].decode('latin1')
    def w(s,o,n=1): return struct.unpack_from('>%dh'%n,s.b,o)
    def chain(s):
        p=s.H[8]; out=[]
        while True:
            L=s.w(p)[0]
            if L==0: return out
            out.append((p,s.b[p:p+L])); p+=L
def parse_phoneme(rec):
    L,=struct.unpack_from('>h',rec,0)
    name=rec[2:4].replace(b'\0',b' ').decode('latin1')
    hw=struct.unpack_from('>4H',rec,4)
    items=[]; p=12
    while p<L:
        pid,sz=rec[p],rec[p+1]; items.append((pid,rec[p+2:p+2+sz])); p+=2+sz
    return name,hw,items
def fmt_item(pid,v):
    n=PNAMES[pid] if pid<len(PNAMES) else 'v%02x'%pid
    if pid<0x80: return n+'='+'/'.join('%d@%s'%(v[i+1],'*' if v[i]==0xff else v[i]) for i in range(0,len(v),2))
    return n+'=%d'%struct.unpack('>h',v)[0] if len(v)==2 else n+'='+v.hex()
if __name__=='__main__':
    x=Ivx(sys.argv[1]); print(x.version,'|',x.tag,'| H=',' '.join('%x'%h for h in x.H))
    for p,r in x.chain()[1:]:
        n,hw,it=parse_phoneme(r)
        print('%5x %-3s %s  %s'%(p,repr(n)[1:-1],' '.join('%04x'%h for h in hw),'  '.join(fmt_item(a,v) for a,v in it)))
