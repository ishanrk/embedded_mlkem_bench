.PHONY: test demo clean

test:
	cargo test --workspace --all-targets

demo:
	cargo run -p pqc-poly-explore -- examples/ntruhps2048509.json -o out
	cc -std=c11 -O2 -Wall -Wextra -Werror -c out/kernel.c -Iout -o out/kernel.o
	rustc --edition=2024 --crate-type=lib out/kernel.rs -o out/libkernel.rlib

clean:
	cargo clean
	rm -rf out
