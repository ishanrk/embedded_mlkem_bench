# pqc-poly-bench

RISC-V/PQC polynomial-multiplication co-design for NTRU-HPS-2048-509.

Step 1 defines the ring and its exact reference multiplication.

Step 2 adds regular multiplication for 509 coefficients, a direct 255/254 one-split Karatsuba, and 512-padded recursive Karatsuba with leaf sizes 32 and 16. Every run records arithmetic counts, logical coefficient loads and stores, peak live two-byte temporary buffers, host time, and reference verification.
