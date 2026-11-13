import os, struct, sys
img=open(sys.argv[1],'rb'); src=sys.argv[2]
def rd(off,n): img.seek(off); return img.read(n)
vd=rd(0x10000,0x800); assert vd[:20]==b'MICROSOFT*XBOX*MEDIA' and vd[0x7EC:0x800]==b'MICROSOFT*XBOX*MEDIA'
rs,rsz=struct.unpack_from('<II',vd,20)
def key(s): return bytes(c-32 if 97<=c<=122 else c for c in s)
def lookup(sector,size,name):
    tbl=rd(sector*2048,size); off=0
    while True:
        l,r,sec,sz,attr,nl=struct.unpack_from('<HHIIBB',tbl,off)
        nm=tbl[off+14:off+14+nl]
        a,b=key(name.encode()),key(nm)
        if a==b: return sec,sz,attr
        nxt = l if a<b else r
        if nxt==0: return None
        off=nxt*4
n=0
for root,dirs,files in os.walk(src):
    if '$SystemUpdate' in root: continue
    rel=os.path.relpath(root,src)
    parts=[] if rel=='.' else rel.split(os.sep)
    for f in files:
        sec,sz=rs,rsz
        for p in parts+[f.upper() if n%2 else f]:   # also test case-insensitivity
            res=lookup(sec,sz,p); assert res, (parts,f)
            sec,sz,attr=res
        data=rd(sec*2048,sz); assert data==open(os.path.join(root,f),'rb').read(), f
        n+=1
print("BST lookups OK for",n,"files")
