import os,pefile,capstone,re,sys,struct,collections,json
pe=pefile.PE(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','bin','Ivx230nt.dll'))
B=pe.OPTIONAL_HEADER.ImageBase
img=pe.get_memory_mapped_image()
text=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
ts=B+text.VirtualAddress; te=ts+text.Misc_VirtualSize
def rd(va,n): return img[va-B:va-B+n]
def cstr(va):
    if not (B+0x40000<=va<B+0x52000): return None
    b=img[va-B:va-B+200]; e=b.find(b'\0')
    if e<3: return None
    s=b[:e]
    if all(32<=c<127 or c in (9,10,13) for c in s): return s.decode()
    return None
imports={}
for d in pe.DIRECTORY_ENTRY_IMPORT:
    for i in d.imports: imports[i.address]=(d.dll.decode()+'!'+(i.name.decode() if i.name else '#%d'%i.ordinal))
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.detail=False
code=rd(ts,te-ts)
insns=[]
off=0
while off<len(code):
    got=False
    for i in md.disasm(code[off:],ts+off):
        insns.append((i.address,i.size,i.mnemonic,i.op_str)); off=i.address+i.size-ts; got=True
    if off<len(code) and True:
        insns.append((ts+off,1,'db',hex(code[off]))); off+=1
calls=collections.Counter(); xref=collections.defaultdict(list)
hexre=re.compile(r'0x[0-9a-f]+')
out=open(sys.argv[1],'w')
for a,sz,m,o in insns:
    cm=''
    for h in hexre.findall(o):
        v=int(h,16)
        if v in imports: cm+=' ; '+imports[v]
        s=cstr(v)
        if s: cm+=' ; "%s"'%s[:70].replace('\n','\n')
        if B<=v<B+0x60000: xref[v].append(a)
    if m=='call' and o.startswith('0x'): calls[int(o,16)]+=1
    out.write('%08x: %-7s %s%s\n'%(a,m,o,cm))
json.dump({'calls':{hex(k):v for k,v in calls.items()},'xref':{hex(k):[hex(x) for x in v] for k,v in xref.items()}},open(sys.argv[2],'w'))
print(len(insns),'insns',len(calls),'call targets')
