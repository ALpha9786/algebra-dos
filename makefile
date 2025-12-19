# Makefile for Algebra OS

# Compiler and linker settings
CC = gcc
LD = ld
ASM = nasm

# Flags
CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -O2 -Wall -Wextra
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

# Output files
KERNEL = kernel.bin
ISO = algebra_os.iso

# Object files
OBJS = kernel.o

# Default target
all: $(KERNEL) $(ISO)

# Compile kernel.c
kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

# Link kernel
$(KERNEL): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $(KERNEL)

# Create bootable ISO
$(ISO): $(KERNEL)
	@mkdir -p isodir/boot/grub
	@cp $(KERNEL) isodir/boot/kernel.bin
	@echo 'set timeout=0' > isodir/boot/grub/grub.cfg
	@echo 'set default=0' >> isodir/boot/grub/grub.cfg
	@echo 'menuentry "Algebra OS" {' >> isodir/boot/grub/grub.cfg
	@echo '    multiboot /boot/kernel.bin' >> isodir/boot/grub/grub.cfg
	@echo '    boot' >> isodir/boot/grub/grub.cfg
	@echo '}' >> isodir/boot/grub/grub.cfg
	@grub-mkrescue -o $(ISO) isodir 2>/dev/null || echo "Warning: grub-mkrescue not found. ISO creation skipped."
	@echo "Build complete: $(KERNEL)"
	@[ -f $(ISO) ] && echo "Bootable ISO: $(ISO)" || true

# Run in QEMU
run: $(ISO)
	qemu-system-i386 -cdrom $(ISO)

# Run kernel directly (without ISO)
run-kernel: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL)

# Clean build files
clean:
	rm -f $(OBJS) $(KERNEL) $(ISO)
	rm -rf isodir

# Rebuild everything
rebuild: clean all

# Help
help:
	@echo "Algebra OS Build System"
	@echo "======================="
	@echo "Targets:"
	@echo "  all         - Build kernel and ISO (default)"
	@echo "  run         - Build and run in QEMU (from ISO)"
	@echo "  run-kernel  - Run kernel directly in QEMU"
	@echo "  clean       - Remove build files"
	@echo "  rebuild     - Clean and build"
	@echo ""
	@echo "Requirements:"
	@echo "  - gcc (with 32-bit support)"
	@echo "  - ld"
	@echo "  - grub-mkrescue (for ISO)"
	@echo "  - qemu-system-i386 (for testing)"

.PHONY: all run run-kernel clean rebuild help
# make command to build iso: make iso
