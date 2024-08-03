CPPFLAGS = -g -std=c++23 -Wall -fpic -ffreestanding -fno-stack-protector -fno-exceptions -fno-rtti -nostdlib -mno-red-zone -mgeneral-regs-only
LDFLAGS = -g -nostdlib
BUILD_DIR := build

$(BUILD_DIR)/serial.elf: $(BUILD_DIR)/serial.o ${BUILD_DIR}/syscall.o
	ld $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/serial.o: serial.cpp
	mkdir -p ${@D}
	g++ $(CPPFLAGS) -MMD -MP -c $< -o $@ 

$(BUILD_DIR)/syscall.o: syscall.asm
	nasm -f elf64 -o $@ $< 

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/*

-include ${BUILD_DIR}/serial.d
