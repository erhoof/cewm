"""Survey compression flag distribution across all type-8 RECORD blocks in a vol."""
import struct, sys
from collections import Counter
path = sys.argv[1]
with open(path,'rb') as f: data=f.read()
def r32(o): return struct.unpack_from('<I',data,o)[0]
def r16(o): return struct.unpack_from('<H',data,o)[0]
scan, end = 0x5000, len(data)
cflags = Counter()
insize_flag_hi = Counter()  # combinations of bit 0x8000 / 0x4000 on the insize1 header word
high_absent = 0
high_present = 0
nrec = 0
while scan + 12 < end:
    hdr = r32(scan); typ=(hdr>>28)&0xF; size=hdr&0x0FFFFFFC; free=hdr&1
    d = scan + 12
    if size==0 or d+size>end: break
    if typ==8 and not free:
        nrec += 1
        wLen = r16(d+8); wCom=r16(d+10)
        cflag = wLen & 0xC000
        cflags[cflag] += 1
        # find propid end
        p = d+12
        while p+4 <= d+size:
            pid = r32(p); p += 4
            if pid & 0x100: break
        # first 2 bytes of compressed data = insize1
        if cflag == 0x4000 and p + 2 <= d + size:
            insize1 = r16(p)
            flags = insize1 & 0xC000
            insize_flag_hi[flags] += 1
            low_len = insize1 & 0x3FFF
            if wCom - low_len == 2:
                high_absent += 1
            else:
                high_present += 1
    scan += 12 + size
print(f"records: {nrec}")
print(f"compression flags (wLen & 0xC000):")
for k, v in cflags.items(): print(f"  0x{k:04X}: {v}")
print(f"StringDecompress insize1 top bits distribution:")
for k, v in insize_flag_hi.items(): print(f"  0x{k:04X}: {v}")
print(f"  high stream absent: {high_absent}, present: {high_present}")
