import os, struct, sys, zlib

# Extracts every file of a VFS bank, e.g. guiscripts.bnk (the GUI screens: .bgf/.bsg/.fac,
# Gameface Lua). Usage: guiextract.py <bank .bnk> <outdir>; the .bnk.dat must sit beside it.
# The .bnk index is a run of zlib streams (big-endian: u32 ?, u32 count, then per entry hash,
# offset, size, packed size, chunk count + chunk sizes; then the names, each u32 length + name +
# 28 bytes). Packed entries are zlib streams, one per 0x8000 packed bytes.

bnk, out = sys.argv[1], sys.argv[2]
raw = open(bnk, 'rb').read()
index, p = b'', 0
while True:
    i = raw.find(b'\x78\xda', p)
    if i < 0:
        break
    z = zlib.decompressobj()
    try:
        index += z.decompress(raw[i:])
        p = len(raw) - len(z.unused_data)
    except zlib.error:
        p = i + 1

count = struct.unpack_from('>I', index, 4)[0]
p, entries = 8, []
for _ in range(count):
    _, off, size, packed, chunks = struct.unpack_from('>5I', index, p)
    p += 20 + 4 * chunks
    entries.append((off, size, packed, chunks))
names = []
for _ in range(count):
    n = struct.unpack_from('>I', index, p)[0]
    p += 4
    names.append(index[p:p + n].rstrip(b'\0').decode())
    p += n + 28

dat = open(bnk + '.dat', 'rb').read()
os.makedirs(out, exist_ok=True)
for (off, size, packed, chunks), name in zip(entries, names):
    data = dat[off:off + packed]
    if size != packed:
        data = b''.join(zlib.decompressobj().decompress(data[k * 0x8000:(k + 1) * 0x8000]) for k in range(chunks))
    if len(data) != size:
        print('short', name, len(data), size)
    open(os.path.join(out, name.split('\\')[-1]), 'wb').write(data)
print(count, 'files')
