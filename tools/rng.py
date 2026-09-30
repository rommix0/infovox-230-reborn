import sys
lo=int(sys.argv[1],16); hi=int(sys.argv[2],16)
for l in open('full.asm'):
    try: a=int(l[:8],16)
    except: continue
    if a>=hi: break
    if a>=lo and not l.rstrip().endswith(('nop','int3')): sys.stdout.write(l)
