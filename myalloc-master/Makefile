CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g

# Optional switches, also documented in DESIGN.md:
#   make CFLAGS="$(CFLAGS) -DMYALLOC_DEBUG=1"
#   make CFLAGS="$(CFLAGS) -DMYALLOC_FIT=1"          # best-fit
#   make CFLAGS="$(CFLAGS) -DMYALLOC_FIT=2"          # next-fit (fails test 7)
#   make CFLAGS="$(CFLAGS) -DMYALLOC_ALIGN=16"        # fails test 5.3
#   make CFLAGS="$(CFLAGS) -DMYALLOC_AUTO_COALESCE=1" # fails tests 5.1 and 6.1

TARGET = allocator
SRC = myalloc.c alligator.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
