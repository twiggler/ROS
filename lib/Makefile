CPPFLAGS = -g -std=c++20 -Wall -fpic -ffreestanding -fno-stack-protector -fno-exceptions -fno-rtti -nostdlib -mno-red-zone -mgeneral-regs-only \
		-I./include
SRC_DIR := src
OBJ_DIR := build
SRC_FILES := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/*/*.cpp)
OBJ_FILES := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRC_FILES))

$(OBJ_DIR)/libtwig.a: $(OBJ_FILES) 
	ar rcs $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	g++ $(CPPFLAGS) -MMD -MP -c -o $@ $<
