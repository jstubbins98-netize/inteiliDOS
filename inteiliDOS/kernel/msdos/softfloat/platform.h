/* inteiliDOS SoftFloat port, MPL-2.0. Integer-only, little-endian IA-32.
 * Use the memory-form API, without FAST_INT64 or host/compiler FP types.
 * No global state is shared with native programs; the DOS interpreter is
 * single-threaded and supplies rounding/flags before each instruction. */
#ifndef MSDOS_SOFTFLOAT_PLATFORM_H
#define MSDOS_SOFTFLOAT_PLATFORM_H
#define LITTLEENDIAN 1
#define INLINE static inline
#define SOFTFLOAT_ROUND_ODD
#endif
