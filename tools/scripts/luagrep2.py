import sys,glob
src=open('luadis.py').read().split("d=open(sys.argv[1]")[0]
exec(src)
wants=sys.argv[1:]
for fn in glob.glob('bnk/all/*.lua'):
    d=open(fn,'rb').read()
    try:
        r=R(d); f=func(r)
    except Exception: continue
    def walk(f):
        ks=[k for k in f['k'] if isinstance(k,str)]
        if all(w in ks for w in wants):
            print(fn.split('scripts_')[-1],'|',f['fname'],'| K:',ks[:60])
        for p in f['p']: walk(p)
    walk(f)
