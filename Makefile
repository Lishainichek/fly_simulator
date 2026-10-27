CC = clang
CFLAGS = -Wall -Wextra -Werror -std=c11 -Iinc

SRC = $(wildcard src/*.c)

fly_simulator: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o fly_simulator

clean:
	rm -f fly_simulator
	