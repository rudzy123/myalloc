CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g

# Defaults and the pass/fail matrix live in myalloc_config.h and DESIGN.md.
# A command-line -D overrides that header. Replace CFLAGS entirely, for example:
#   make run CFLAGS="-Wall -Wextra -std=c11 -g -DMYALLOC_FIT=1"
# `make run` does not build stress.c.

TARGET = allocator
SRC = myalloc.c alligator.c
HDR = myalloc.h myalloc_config.h
SAN_FLAGS = -O1 -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all run clean san stress

all: $(TARGET)

$(TARGET): $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

san: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SAN_FLAGS) $(SRC) -o allocator-san
	./allocator-san

stress: stress.c myalloc.c $(HDR)
	$(CC) $(CFLAGS) stress.c myalloc.c -o stress
	./stress

clean:
	rm -f $(TARGET) stress allocator-san
