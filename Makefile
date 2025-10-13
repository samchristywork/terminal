CC = gcc
CFLAGS = -I/usr/include/freetype2

build/gui: build/gui.o build/terminal.o build/args.o
	${CC} build/gui.o build/terminal.o build/args.o -o $@ -lX11 -lXft -lXrender -lfontconfig

build/%.o: src/%.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build
