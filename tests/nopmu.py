import struct,sys,subprocess
p=sys.argv[1]; d=bytearray(open(p,'rb').read())
# .text file range
o=subprocess.check_output(['arm-none-eabi-readelf','-SW',p]).decode()
for l in o.splitlines():
    f=l.replace('[','').replace(']','').split()
    if len(f)>5 and f[1]=='.text': off,size=int(f[4],16),int(f[5],16)
n=0
for i in range(off,off+size,4):
    w=struct.unpack_from('<I',d,i)[0]
    if w&0xffff0fff in(0xee190f1d,0xee190f5d):
        struct.pack_into('<I',d,i,0xe3a00000|(w&0xf000)); n+=1
    elif w&0xffff0fff==0xee090fbc:
        struct.pack_into('<I',d,i,0xe320f000); n+=1
open(p,'wb').write(d); print('patched',n)
