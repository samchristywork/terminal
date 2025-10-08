CC = gcc

build/main: build/main.o build/terminal.o
	${CC} build/*.o -o $@

build/test: build/test.o build/terminal.o
	${CC} build/*.o -o $@

build/example: build/example.o build/terminal.o
	${CC} build/*.o -o $@

build/gui: build/gui.o build/terminal.o build/args.o
	${CC} build/gui.o build/terminal.o build/args.o -o $@ -lX11

build/%.o: src/%.c
	mkdir -p build
	$(CC) -c $< -o $@

run: build/main
	./build/main

test: build/test
	./build/test

clean:
	rm -rf build
