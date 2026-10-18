import sys,re,glob
src=open('luadis.py').read().split("d=open(sys.argv[1]")[0]
exec(src)
want=sys.argv[1]
for fn in glob.glob('bnk/all/*.lua'):
    d=open(fn,'rb').read()
    try:
        r=R(d); f=func(r)
    except Exception as e: continue
    def walk(f):
        if any(k==want for k in f['k'] if isinstance(k,str)):
            print(fn.split('scripts_')[-1], '|', f['fname'], '| K:', [k for k in f['k'] if isinstance(k,str)][:40])
        for p in f['p']: walk(p)
    walk(f)
