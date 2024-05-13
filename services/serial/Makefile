CPPFLAGS = -g -std=c++20 -Wall -fpic -ffreestanding -fno-stack-protector -fno-exceptions -fno-rtti -nostdlib -mno-red-zone -mgeneral-regs-only
LDFLAGS = -g -nostdlib
DEPS := deps.d
BUILD_DIR := build

$(BUILD_DIR)/serial.elf: $(BUILD_DIR)/serial.o
	ld $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/serial.o: serial.cpp
	mkdir -p ${@D}
	g++ $(CPPFLAGS) -MMD -MP -MF $(DEPS) -c -o $@ $<

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/*

-include ${DEPS}
