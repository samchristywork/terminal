CC = gcc

build/main: build/main.o build/terminal.o
	${CC} build/*.o -o $@

build/test: build/test.o build/terminal.o
	${CC} build/*.o -o $@

build/%.o: src/%.c
	mkdir -p build
	$(CC) -c $< -o $@

run: build/main
	./build/main

test: build/test
	./build/test

clean:
	rm -rf build
