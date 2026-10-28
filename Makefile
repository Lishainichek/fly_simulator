CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L -Iinc -pthread
SOURCES = $(wildcard src/*.c)
CORE = $(filter-out src/main.c,$(SOURCES))

all: fly_simulator

fly_simulator: $(SOURCES) $(wildcard inc/*.h)
	$(CC) $(CFLAGS) $(SOURCES) -o $@

test: tests/test_sim
	./tests/test_sim

tests/test_sim: $(CORE) tests/test_sim.c $(wildcard inc/*.h)
	$(CC) $(CFLAGS) $(CORE) tests/test_sim.c -o $@

clean:
	rm -f fly_simulator tests/test_sim

.PHONY: all test clean
