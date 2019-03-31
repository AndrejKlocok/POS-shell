SRC_DIR  	= .
BUILD_DIR	= .


# Compiler/Linker required information
CXX      	= gcc
CFLAGS		= -std=c11 -Wall -pedantic -O2 -pthread
TARGET		= shell

SRC_FILES   = $(wildcard $(SRC_DIR)/*.c)	
OBJ			= main.o
OBJ_BUILDED = $(addprefix $(BUILD_DIR)/,$(OBJ))

all: $(TARGET)

$(BUILD_DIR)/%.o:$(SRC_DIR)/%.c 
	$(CXX) $(CFLAGS) -c -I$(SRC_DIR) $< -o $@

$(TARGET): $(OBJ_BUILDED)
	$(CXX)  $(CFLAGS) $(OBJ_BUILDED) -o $@


run:
	./$(TARGET)
clean:
	rm  $(TARGET)
	rm --recursive --force $(OBJ_BUILDED)