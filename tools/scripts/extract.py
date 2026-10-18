import struct,zlib,re,os,sys
o=open('bnk/gamescripts.idx','rb').read()
cnt=struct.unpack_from('>I',o,4)[0]
p=8; ents=[]
for i in range(cnt):
    h,off,us,cs,n=struct.unpack_from('>5I',o,p); p+=20
    extra=struct.unpack_from('>%dI'%n,o,p); p+=4*n
    ents.append((h,off,us,cs,extra))
print('entries end',hex(p),len(o), ents[:3], ents[-2:])
print(o[p:p+64])
names=[]
for i in range(cnt):
    l=struct.unpack_from('>I',o,p)[0]; p+=4
    names.append(o[p:p+l].rstrip(b'\0').decode()); p+=l+28
print(names[:2], names[-2:], p, len(o))
dat=open(r'F:\Fit Games\Fable 3\data\gamescripts.bnk.dat','rb').read()
want=sys.argv[1:]
os.makedirs('bnk/out',exist_ok=True)
for (h,off,us,cs,extra),nm in zip(ents,names):
    if not any(w in nm for w in want): continue
    raw=dat[off:off+cs]
    z=zlib.decompressobj(); data=z.decompress(raw)
    if len(data)!=us: print('short',nm,len(data),us)
    fn='bnk/out/'+nm.split(chr(92))[-1]
    open(fn,'wb').write(data); print(nm,len(data),data[:12])
