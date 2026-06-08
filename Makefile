CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
SRC = src/main.c src/parser.c src/executor.c
OBJ = $(SRC:.c=.o)
TARGET = bin/7sh

# Header dependency tracking so changes in include/*.h trigger a rebuild
DEPS = include/shell.h include/parser.h

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p bin
	$(CC) $(OBJ) -o $(TARGET)

# Include DEPS here to monitor header modifications
%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf src/*.o bin/