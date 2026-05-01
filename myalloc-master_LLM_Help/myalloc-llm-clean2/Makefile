CC = gcc
CFLAGS = -Wall -Wextra -g

TARGET = allocator
SRC = myalloc.c alligator.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
run: $(TARGET)
	./$(TARGET)
clean:
	rm -f $(TARGET)
