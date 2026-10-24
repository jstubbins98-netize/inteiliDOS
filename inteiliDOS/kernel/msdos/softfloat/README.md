# Berkeley SoftFloat (integer-only x87 arithmetic)

Source: https://www.jhauser.us/arithmetic/SoftFloat-3e.zip
Release 3e, 2018-01-20. Archive SHA-256:
`21130ce885d35c1fe73fc1e1bf2244178167e05c6747cad5f450cc991714c746`.

The selected upstream sources and headers are unmodified. The local
`platform.h` selects little-endian memory-form operations without
`SOFTFLOAT_FAST_INT64`. This subset uses the `8086` NaN/invalid-result
specialization and only includes the transitive dependencies of the
extF80 arithmetic/comparison/square-root/remainder/rounding and
32/64-bit real and signed-integer conversion entry points.
No float16/float128 arithmetic, host floating-point types, libm, threads,
allocation, physical device access, or libc are required.
The upstream BSD-3-Clause notice is retained in `LICENSE` and in every
upstream source file. Binary distributions must retain that notice too.

Why this implementation:

- The pinned tiny386 upstream `fpu.c` says it has no exceptions or tag
  word, uses host `double`/libm, and optionally native `__float80`.
  Importing it would violate the freestanding, no-host-FPU requirement.
- SoftFloat 2c has an older restrictive notice, not the release-3 BSD
  license. It was evaluated but is **not** included.
- SoftFloat 3e supplies integer-only IEEE arithmetic, explicit rounding,
  exception flags, and genuine 80-bit extended range/precision. The local
  `../tiny386/fpu.c` supplies x87 decoding, tags, stack, exceptions,
  environments, BCD and bounded software transcendental approximations.

SoftFloat 3's exception bit layout differs from x87. The adapter explicitly
maps these flags; never copy SoftFloat exception bits directly into SW.
Arithmetic rounding/exception state is selected afresh for each interpreted
instruction. Only the single-threaded DOS interpreter uses this library.

Builds keep the native `-march=i386 -msoft-float` baseline. The test
`tests/run_msdos_x87_cpu_test.py` audits the generated objects for host
x87/MMX/SSE instructions as well as running the CPU fixture.
