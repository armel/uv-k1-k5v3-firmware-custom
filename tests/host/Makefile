# Host-side tests for the dock protocol core (App/app/dock.c).
# Pure C, no firmware/hardware dependencies — builds and runs on the host.
#
#   make -C tests/host run

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2
INCLUDES = -I../../App

SRC = test_dock.c ../../App/app/dock.c

test_dock: $(SRC) ../../App/app/dock.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SRC)

run: test_dock
	./test_dock

clean:
	rm -f test_dock

.PHONY: run clean
