CPPFLAGS = -g -std=c++20 -Wall -fpic -ffreestanding -fno-stack-protector -fno-exceptions -fno-rtti -nostdlib -mno-red-zone -mgeneral-regs-only \
		-I./bootboot/dist/ -I../lib/include
LDFLAGS = -g -nostdlib -L ../lib/build -T link.ld
LDLIBS = -ltwig
SRC_DIR := src
OBJ_DIR := build
SRC_FILES := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/*/*.cpp)
OBJ_FILES := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRC_FILES))

$(OBJ_DIR)/kernel.x86_64.elf: $(OBJ_FILES) ${OBJ_DIR}/cpu_asm.o $(OBJ_DIR)/font.o
	ld $(LDFLAGS) -o $@ $^ ${LDLIBS}
	readelf -hls $@ > $(OBJ_DIR)/kernel.x86_64.txt

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	g++ $(CPPFLAGS) -MMD -MP -c -o $@ $<

$(OBJ_DIR)/cpu_asm.o: $(SRC_DIR)/cpu/cpu.asm
	nasm -f elf64 -o $@ $< 

$(OBJ_DIR)/font.o: font/font.psf
	ld -r -z noexecstack -b binary -o $@ $<
