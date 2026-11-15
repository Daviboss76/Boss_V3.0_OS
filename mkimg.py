#!/usr/bin/env python3
# Monta disk.img: MBR + estagio 2 (setores 1-50) + particao FAT32 com KERNEL.BIN
import struct, subprocess

IMG, SIZE_MB, PART_LBA = 'disk.img', 64, 2048

mbr    = open('mbr.bin', 'rb').read()
stage2 = open('stage2.bin', 'rb').read()
kernel = open('kernel.bin', 'rb').read()
assert len(mbr) == 512 and len(stage2) <= (PART_LBA - 1) * 512

# 1. imagem vazia + FAT32 na particao (offset em setores)
with open(IMG, 'wb') as f:
    f.truncate(SIZE_MB << 20)
subprocess.run(['mkfs.fat', '-F', '32', '--offset', str(PART_LBA), IMG], check=True)

img = open(IMG, 'r+b')
def rd(off, n): img.seek(off); return img.read(n)
def wr(off, data): img.seek(off); img.write(data)

# 2. MBR: codigo (446 bytes) + tabela de particoes + assinatura
total = SIZE_MB << 11
entry = struct.pack('<B3sB3sII', 0x80, b'\xfe\xff\xff', 0x0C,
                    b'\xfe\xff\xff', PART_LBA, total - PART_LBA)
sector0 = bytearray(mbr[:446]) + entry + bytes(48) + b'\x55\xaa'
wr(0, bytes(sector0))

# 3. Estagio 2 nos setores 1..50
wr(512, stage2)

# 4. KERNEL.BIN no FAT32
base = PART_LBA * 512
bpb = rd(base, 512)
bps, spc, rsv, nfat = struct.unpack_from('<HBHB', bpb, 0x0B)
fatsz, = struct.unpack_from('<I', bpb, 0x24)
root,  = struct.unpack_from('<I', bpb, 0x2C)
assert bps == 512 and struct.unpack_from('<H', bpb, 0x16)[0] == 0, 'nao e FAT32'

fat_off  = base + rsv * bps
data_off = fat_off + nfat * fatsz * bps
cs = spc * bps                                  # bytes por cluster
n  = -(-len(kernel) // cs)                      # clusters necessarios

fat = bytearray(rd(fat_off, fatsz * bps))
free, c = [], 2
while len(free) < n:
    if struct.unpack_from('<I', fat, c * 4)[0] & 0x0FFFFFFF == 0:
        free.append(c)
    c += 1
for i, c in enumerate(free):
    nxt = free[i + 1] if i + 1 < n else 0x0FFFFFFF
    struct.pack_into('<I', fat, c * 4, nxt)
    chunk = kernel[i * cs:(i + 1) * cs].ljust(cs, b'\0')
    wr(data_off + (c - 2) * cs, chunk)
for k in range(nfat):
    wr(fat_off + k * fatsz * bps, bytes(fat))

# entrada no diretorio raiz (primeiro slot livre)
rootoff = data_off + (root - 2) * cs
rdir = rd(rootoff, cs)
for p in range(0, cs, 32):
    if rdir[p] in (0x00, 0xE5):
        ent = struct.pack('<11sBBBHHHHHHHI', b'KERNEL  BIN', 0x20, 0, 0,
                          0, 0, 0, free[0] >> 16, 0, 0, free[0] & 0xFFFF,
                          len(kernel))
        wr(rootoff + p, ent)
        break

img.close()
print(f'OK: KERNEL.BIN = {len(kernel)} bytes em {n} cluster(s), '
      f'primeiro = {free[0]}')
