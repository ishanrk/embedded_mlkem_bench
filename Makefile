.PHONY: all build test sanitize clean

all: test

build:
	cmake --preset release
	cmake --build --preset release --parallel

test: build
	ctest --preset release

sanitize:
	cmake --preset sanitize
	cmake --build --preset sanitize --parallel
	ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize

clean:
	cmake -E remove_directory build
