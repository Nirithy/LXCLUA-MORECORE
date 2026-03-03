CC = gcc
CFLAGS = -m32 -ffreestanding -nostdlib -O2 -Wall -Wextra
AS = as
ASFLAGS = --32
LD = ld
LDFLAGS = -m elf_i386 -T linker.ld

OBJS = boot.o kernel.o vga.o serial.o gdt.o idt.o memory.o isr.o x86.o interrupt.o

all: lxcluaos.bin

lxcluaos.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) -c $< -o $@

clean:
	rm -f *.o lxcluaos.bin
