import json,sys
x=json.load(open('xref.json'))['xref']
lo=int(sys.argv[1],16); hi=int(sys.argv[2],16) if len(sys.argv)>2 else lo+1
for k in sorted(x,key=lambda k:int(k,16)):
    if lo<=int(k,16)<hi: print(k, ' '.join(x[k][:20]))
