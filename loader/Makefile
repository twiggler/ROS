CPPFLAGS = -g -std=c++23 -Wall -fpic -ffreestanding -fno-stack-protector -fno-exceptions -fno-rtti -nostdlib -mno-red-zone -mgeneral-regs-only \
		-I./bootboot/dist/ -I../libr/include -I../kernel/include
LDFLAGS = -g -nostdlib -L ../libr/build -L ../kernel/build -T link.ld
LDLIBS = -lkernel -lr 
DEPS := deps.d
BUILD_DIR := build
INITRD_DIR := $(BUILD_DIR)/initrd

$(INITRD_DIR): CONFIG $(BUILD_DIR)/kernel.x86_64.elf
	mkdir -p $(INITRD_DIR)/EFI/BOOT
	mkdir -p $(INITRD_DIR)/BOOTBOOT
	cp bootboot/dist/bootboot.efi $(INITRD_DIR)/EFI/BOOT/BOOTX64.EFI
	cp CONFIG $(INITRD_DIR)/BOOTBOOT/CONFIG
	tar --create --format=ustar --file=$(INITRD_DIR)/BOOTBOOT/INITRD \
		--directory=$(BUILD_DIR) kernel.x86_64.elf \
		--directory=../../services/serial/build serial.elf

$(BUILD_DIR)/kernel.x86_64.elf: $(BUILD_DIR)/main.o ../kernel/build/libkernel.a ../libr/build/libr.a
	ld $(LDFLAGS) -o $@ $^ ${LDLIBS}

$(BUILD_DIR)/main.o: main.cpp
	mkdir -p ${@D}
	g++ $(CPPFLAGS) -MMD -MP -MF $(DEPS) -c -o $@ $<

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/*

-include ${DEPS}
