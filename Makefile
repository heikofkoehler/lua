.PHONY: all build clean test

all: build

build:
	mkdir -p build && cd build && cmake .. && make

clean:
	rm -rf build

test: build
	cd build && make test
