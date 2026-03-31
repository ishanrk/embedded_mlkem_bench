.PHONY: all build test native benchmark tune-example sanitize clean

all: native

build:
	cmake --preset release
	cmake --build --preset release --parallel

test: build
	ctest --preset release

native:
	cmake --preset release-native
	cmake --build --preset release-native --parallel
	ctest --preset release-native

benchmark: native
	./build/release-native/pqc-poly-formula-bench 1000

tune-example: build
	./build/release/pqc-poly-bench --tune-host examples/host-negacyclic.json -o out

sanitize:
	cmake --preset sanitize
	cmake --build --preset sanitize --parallel
	ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize

clean:
	cmake -E remove_directory build
	cmake -E remove_directory out
