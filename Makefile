CC     = clang --target=i386-none-elf
CFLAGS = -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
         -fno-asynchronous-unwind-tables -fno-strict-aliasing -nostdlib \
         -mno-sse -mno-mmx -O2 -Wall -Wextra -Iinclude

KSRCS = kernel/kernel.c kernel/heal.c kernel/ring3.c kernel/drv.c kernel/blk.c kernel/sched.c kernel/net.c kernel/ota.c  kernel/ota_net.c \
        arch/gdt.c arch/idt.c arch/pic.c arch/paging.c \
        lib/lib.c \
        drivers/vga.c drivers/keyboard.c drivers/rtc.c drivers/timer.c \
        drivers/pci.c drivers/drivers.c drivers/rtl8139.c drivers/ide.c drivers/ahci.c \
        drivers/ehci.c drivers/usb.c drivers/e1000.c \
        fs/fat32.c fs/fsck.c fs/part.c \
        ui/ui.c ui/terminal.c ui/splash.c ui/disktool.c ui/usbtool.c
KOBJS = $(KSRCS:.c=.o)
HDRS  = $(wildcard include/*.h user/*.h apps/*.h)

UDRV  = rtc beep crc bug intel_e1000 mouse_r3
UBINS = $(addprefix user/,$(addsuffix .bin,$(UDRV)))
APPS  = hello count crashme
APPELFS = $(addprefix apps/,$(addsuffix .elf,$(APPS)))

all: disk.img

disk.img: kernel.bin mbr.bin stage2.bin mkimg.py $(APPELFS)
	python mkimg.py

kernel.bin: kernel.elf
	llvm-objcopy -O binary $< $@

kernel.elf: entry.o arch/arch.o user/blobs.o boot/bootblobs.o $(KOBJS) linker.ld
	ld.lld -m elf_i386 -T linker.ld -nostdlib entry.o arch/arch.o user/blobs.o boot/bootblobs.o $(KOBJS) -o $@

entry.o: entry.asm
	nasm -f elf32 $< -o $@

arch/arch.o: arch/arch.asm
	nasm -f elf32 $< -o $@

user/blobs.o: user/blobs.asm $(UBINS)
	nasm -f elf32 -I user/ $< -o $@

boot/bootblobs.o: boot/bootblobs.asm mbr.bin stage2.bin
	nasm -f elf32 -I ./ $< -o $@

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

user/%.elf: user/%.o user/user.ld
	ld.lld -m elf_i386 -T user/user.ld -nostdlib $< -o $@

user/%.bin: user/%.elf
	llvm-objcopy -O binary $< $@

apps/%.elf: apps/%.o user/prog.ld
	ld.lld -m elf_i386 -T user/prog.ld -nostdlib $< -o $@

mbr.bin: mbr.asm
	nasm -f bin $< -o $@

stage2.bin: stage2.asm
	nasm -f bin $< -o $@

pen.img:
	truncate -s 256M pen.img

run: disk.img
	qemu-system-i386 -m 64 -drive format=raw,file=disk.img -display curses

run-ahci: disk.img
	qemu-system-i386 -m 64 -machine q35 -drive format=raw,file=disk.img -display curses

run-usb: disk.img pen.img
	qemu-system-i386 -m 64 -drive format=raw,file=disk.img -device usb-ehci,id=ehci \
	  -drive if=none,id=pen,format=raw,file=pen.img -device usb-storage,bus=ehci.0,drive=pen \
	  -display curses

clean:
	rm -f $(KOBJS) entry.o arch/arch.o boot/*.o user/*.o user/*.elf user/*.bin apps/*.o apps/*.elf *.elf *.bin disk.img
