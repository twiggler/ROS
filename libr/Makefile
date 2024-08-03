CPPFLAGS = -g -std=c++23 -Wall -fpic -ffreestanding -fno-stack-protector -fno-exceptions -fno-rtti -nostdlib -mno-red-zone -mgeneral-regs-only \
		-I./include
SRC_DIR := src
OBJ_DIR := build
SRC_FILES := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/*/*.cpp)
OBJ_FILES := $(SRC_FILES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
DEP_FILES := $(OBJ_FILES:.o=.d)

$(OBJ_DIR)/libr.a: $(OBJ_FILES) 
	ar rcs $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	mkdir -p $(@D)
	g++ $(CPPFLAGS) -MMD -MP -c $< -o $@ 

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR)/*

-include: $(DEP_FILES)
