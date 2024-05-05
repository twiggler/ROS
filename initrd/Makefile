.PHONY: initrd
initrd: CONFIG
	mkdir -p build/EFI/BOOT
	mkdir -p build/BOOTBOOT
	cp ../kernel/bootboot/dist/bootboot.efi build/EFI/BOOT/BOOTX64.EFI
	cp CONFIG build/BOOTBOOT/CONFIG
	tar --create --format=ustar --file=build/BOOTBOOT/INITRD --directory=../kernel/build kernel.x86_64.elf
