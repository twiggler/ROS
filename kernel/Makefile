CPPFLAGS = -g -std=c++20 -Wall -fpic -ffreestanding -fno-stack-protector -fno-exceptions -fno-rtti -nostdlib -mno-red-zone -mgeneral-regs-only \
		-I./bootboot/dist/ -I../libr/include -I./include
LDFLAGS = -g -nostdlib -L ../libr/build
LDLIBS = -lr
SRC_DIR := src
OBJ_DIR := build
SRC_FILES := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/*/*.cpp)
OBJ_FILES := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRC_FILES))
DEPS := deps.d

$(OBJ_DIR)/libkernel.a: $(OBJ_FILES) ${OBJ_DIR}/cpu_asm.o $(OBJ_DIR)/font.o ../libr/build/libr.a
	ar rcs $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	g++ $(CPPFLAGS) -MMD -MP -MF $(DEPS) -c -o $@ $<

$(OBJ_DIR)/cpu_asm.o: $(SRC_DIR)/cpu/cpu.asm
	nasm -f elf64 -o $@ $< 

$(OBJ_DIR)/font.o: font/font.psf
	ld $(LDFLAGS) -r -z noexecstack -b binary -o $@ $< $(LDLIBS)

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR)/*

-include ${DEPS}
