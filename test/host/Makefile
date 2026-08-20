# Host-side test for the HID report descriptor parser.
#
# hid_dump.c is deliberately free of ESP-IDF dependencies so it can be built
# and exercised with a normal compiler. Run this after touching hid_dump.c.
#
#   make run

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g
INCLUDE  = -I../../main
SRC      = test_hid_dump.c ../../main/hid_dump.c
BIN      = test_hid_dump

.PHONY: all run clean

all: $(BIN)

$(BIN): $(SRC) ../../main/hid_dump.h
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $(SRC)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
