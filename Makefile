CC = gcc
CFLAGS = -O3 -march=native -ffast-math -Wall -Wextra
LDFLAGS = -lm -lgpiod -flto

firmware.out: firmware.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

run: firmware.out
	@time ./firmware.out

clean:
	rm -f *.out
