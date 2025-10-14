CC = gcc
CFLAGS = -I/usr/include/freetype2 -Wall -Wextra -O2
LIBS = -lX11 -lXft -lXrender -lfontconfig
OBJS = build/gui.o build/terminal.o build/args.o build/log.o

all: gui

gui: build/gui

build/gui: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

build/%.o: src/%.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build

.PHONY: clean all gui
