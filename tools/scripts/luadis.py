import struct,sys
OPS=['MOVE','LOADK','LOADBOOL','LOADNIL','GETUPVAL','GETGLOBAL','GETTABLE','SETGLOBAL','SETUPVAL','SETTABLE','NEWTABLE','SELF','ADD','SUB','MUL','DIV','MOD','POW','UNM','NOT','LEN','CONCAT','JMP','EQ','LT','LE','TEST','TESTSET','CALL','TAILCALL','RETURN','FORLOOP','FORPREP','TFORLOOP','SETLIST','CLOSE','CLOSURE','VARARG']
class R:
    def __init__(s,d): s.d=d; s.p=12
    def u8(s): v=s.d[s.p]; s.p+=1; return v
    def i32(s): v=struct.unpack_from('<i',s.d,s.p)[0]; s.p+=4; return v
    def u32(s): v=struct.unpack_from('<I',s.d,s.p)[0]; s.p+=4; return v
    def f32(s): v=struct.unpack_from('<f',s.d,s.p)[0]; s.p+=4; return v
    def st(s):
        n=s.u32()
        if n==0: return None
        v=s.d[s.p:s.p+n-1].decode('latin1'); s.p+=n; return v
def func(r):
    f={}
    f['src']=r.st(); f['fname']=r.st(); f['line']=r.i32(); r.i32(); f['nups']=r.u8(); f['np']=r.u8(); r.u8(); r.u8()
    n=r.i32(); f['code']=[r.u32() for _ in range(n)]
    n=r.i32(); ks=[]
    for _ in range(n):
        t=r.u8()
        if t==0: ks.append(None)
        elif t==1: ks.append(bool(r.u8()))
        elif t==3: ks.append(r.f32())
        elif t==4: ks.append(r.st())
        else: raise Exception('const type %d at %x'%(t,r.p))
    f['k']=ks
    n=r.i32(); f['p']=[func(r) for _ in range(n)]
    n=r.i32(); f['li']=[r.i32() for _ in range(n)]
    n=r.i32(); f['loc']=[(r.st(),r.i32(),r.i32()) for _ in range(n)]
    n=r.i32(); f['upv']=[r.st() for _ in range(n)]
    return f
def RK(f,x): return repr(f['k'][x-256]) if x>=256 else 'R%d'%x
def dump(f,name,depth=0,filt=None):
    # name the function by the first string constant ending with ':'-style name if possible
    out=[]
    out.append('%s== function %s (line %d, params %d, upv %s)'%('  '*depth,name,f['line'],f['np'],f['upv']))
    for pc,ins in enumerate(f['code']):
        op=ins&0x3f; a=(ins>>6)&0xff; c=(ins>>14)&0x1ff; b=(ins>>23)&0x1ff; bx=ins>>14; sbx=bx-131071
        o=OPS[op] if op<len(OPS) else 'OP%d'%op
        if o in('LOADK','GETGLOBAL','SETGLOBAL'): s='%s R%d %r'%(o,a,f['k'][bx])
        elif o in('GETTABLE','SELF'): s='%s R%d R%d %s'%(o,a,b,RK(f,c))
        elif o=='SETTABLE': s='%s R%d %s %s'%(o,a,RK(f,b),RK(f,c))
        elif o in('EQ','LT','LE','ADD','SUB','MUL','DIV','MOD','POW'): s='%s %d %s %s'%(o,a,RK(f,b),RK(f,c))
        elif o in('JMP','FORLOOP','FORPREP'): s='%s %d -> %d'%(o,sbx,pc+1+sbx)
        elif o=='CLOSURE': s='%s R%d proto%d'%(o,a,bx)
        elif o in('GETUPVAL','SETUPVAL'): s='%s R%d %s'%(o,a,f['upv'][b] if b<len(f['upv']) else b)
        else: s='%s %d %d %d'%(o,a,b,c)
        out.append('%s  %3d %s'%('  '*depth,pc,s))
    for i,p in enumerate(f['p']):
        out+=dump(p,'%s.proto%d'%(name,i),depth+1)
    return out
d=open(sys.argv[1],'rb').read()
r=R(d); f=func(r)
lines=dump(f,'main')
open(sys.argv[2],'w').write('\n'.join(lines))
print(len(lines),'lines; parsed to',hex(r.p),'of',hex(len(d)))
