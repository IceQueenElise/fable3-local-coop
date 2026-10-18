import sys,re
src=open('luadis.py').read().split("d=open(sys.argv[1]")[0]
exec(src)
d=open(sys.argv[1],'rb').read()
r=R(d); f=func(r)
pat=re.compile(sys.argv[2])
def walk(f):
    if f['fname'] and pat.search(f['fname']):
        print('==',f['fname'],'line',f['line'],'ncode',len(f['code']))
        print('  K:',f['k'])
        print('  locals:',[l[0] for l in f['loc']])
        print('  upv:',f['upv'])
    for p in f['p']: walk(p)
walk(f)
