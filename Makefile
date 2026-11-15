CC     = clang --target=i386-none-elf
CFLAGS = -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
         -fno-asynchronous-unwind-tables -fno-strict-aliasing -nostdlib \
         -mno-sse -mno-mmx -O2 -Wall -Wextra -Iinclude

KSRCS = kernel/kernel.c kernel/heal.c kernel/ring3.c kernel/drv.c kernel/blk.c \
        arch/gdt.c arch/idt.c arch/pic.c \
        lib/lib.c \
        drivers/vga.c drivers/keyboard.c drivers/rtc.c drivers/timer.c \
        drivers/pci.c drivers/drivers.c drivers/rtl8139.c drivers/ide.c drivers/ahci.c \
        fs/fat32.c fs/fsck.c \
        ui/ui.c ui/terminal.c ui/splash.c
KOBJS = $(KSRCS:.c=.o)
HDRS  = $(wildcard include/*.h user/*.h)

UDRV  = rtc beep crc bug
UBINS = $(addprefix user/,$(addsuffix .bin,$(UDRV)))

all: disk.img

disk.img: kernel.bin mbr.bin stage2.bin mkimg.py
	python mkimg.py

kernel.bin: kernel.elf
	llvm-objcopy -O binary $< $@

kernel.elf: entry.o arch/arch.o user/blobs.o $(KOBJS) linker.ld
	ld.lld -m elf_i386 -T linker.ld -nostdlib entry.o arch/arch.o user/blobs.o $(KOBJS) -o $@

entry.o: entry.asm
	nasm -f elf32 $< -o $@

arch/arch.o: arch/arch.asm
	nasm -f elf32 $< -o $@

user/blobs.o: user/blobs.asm $(UBINS)
	nasm -f elf32 -I user/ $< -o $@

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

user/%.elf: user/%.o user/user.ld
	ld.lld -m elf_i386 -T user/user.ld -nostdlib $< -o $@

user/%.bin: user/%.elf
	llvm-objcopy -O binary $< $@

mbr.bin: mbr.asm
	nasm -f bin $< -o $@

stage2.bin: stage2.asm
	nasm -f bin $< -o $@

run: disk.img
	qemu-system-i386 -m 64 -drive format=raw,file=disk.img -display curses

run-ahci: disk.img
	qemu-system-i386 -m 64 -machine q35 -drive format=raw,file=disk.img -display curses

clean:
	rm -f $(KOBJS) entry.o arch/arch.o user/*.o user/*.elf user/*.bin *.elf *.bin disk.img
