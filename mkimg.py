#!/usr/bin/env python3
# Monta disk.img: MBR + estagio 2 + particao FAT32 (rotulo BOSSOS) com KERNEL.BIN e apps/*.elf
import struct, subprocess, glob, os

IMG, SIZE_MB, PART_LBA = 'disk.img', 64, 2048
mbr = open('mbr.bin', 'rb').read()
stage2 = open('stage2.bin', 'rb').read()
assert len(mbr) == 512 and len(stage2) <= (PART_LBA - 1) * 512

arquivos = [('KERNEL', 'BIN', open('kernel.bin', 'rb').read())]
for f in sorted(glob.glob('apps/*.elf')):
    stem = os.path.basename(f)[:-4].upper()
    assert len(stem) <= 8, stem
    arquivos.append((stem, 'ELF', open(f, 'rb').read()))
assert len(arquivos) <= 12

with open(IMG, 'wb') as f:
    f.truncate(SIZE_MB << 20)
subprocess.run(['mkfs.fat', '-F', '32', '-n', 'BOSSOS', '--offset', str(PART_LBA), IMG], check=True)

img = open(IMG, 'r+b')
def rd(off, n): img.seek(off); return img.read(n)
def wr(off, data): img.seek(off); img.write(data)

total = SIZE_MB << 11
entry = struct.pack('<B3sB3sII', 0x80, b'\xfe\xff\xff', 0x0C, b'\xfe\xff\xff', PART_LBA, total - PART_LBA)
wr(0, bytes(mbr[:446]) + entry + bytes(48) + b'\x55\xaa')
wr(512, stage2)

base = PART_LBA * 512
bpb = rd(base, 512)
bps, spc, rsv, nfat = struct.unpack_from('<HBHB', bpb, 0x0B)
fatsz, = struct.unpack_from('<I', bpb, 0x24)
root, = struct.unpack_from('<I', bpb, 0x2C)
assert bps == 512 and struct.unpack_from('<H', bpb, 0x16)[0] == 0, 'nao e FAT32'

fat_off = base + rsv * bps
data_off = fat_off + nfat * fatsz * bps
cs = spc * bps
fat = bytearray(rd(fat_off, fatsz * bps))
rootoff = data_off + (root - 2) * cs
rdir = bytearray(rd(rootoff, cs))
proximo = 2

def livre():
    global proximo
    while struct.unpack_from('<I', fat, proximo * 4)[0] & 0x0FFFFFFF:
        proximo += 1
    c = proximo
    proximo += 1
    return c

for stem, ext, dados in arquivos:
    n = -(-len(dados) // cs) if dados else 0
    cl = [livre() for _ in range(n)]
    for i, c in enumerate(cl):
        nxt = cl[i + 1] if i + 1 < n else 0x0FFFFFFF
        struct.pack_into('<I', fat, c * 4, nxt)
        wr(data_off + (c - 2) * cs, dados[i * cs:(i + 1) * cs].ljust(cs, b'\0'))
    for p in range(0, cs, 32):
        if rdir[p] in (0x00, 0xE5):
            primeiro = cl[0] if cl else 0
            ent = struct.pack('<11sBBBHHHHHHHI', (stem.ljust(8) + ext.ljust(3)).encode(),
                              0x20, 0, 0, 0, 0, 0, primeiro >> 16, 0, 0, primeiro & 0xFFFF, len(dados))
            rdir[p:p + 32] = ent
            break
    else:
        raise SystemExit('diretorio raiz cheio')

for k in range(nfat):
    wr(fat_off + k * fatsz * bps, bytes(fat))
wr(rootoff, bytes(rdir))
img.close()
print('OK: ' + ', '.join('%s.%s (%d B)' % (s, e, len(d)) for s, e, d in arquivos))
