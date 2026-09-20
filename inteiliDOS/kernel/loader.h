/*
 * inteiliDOS -- kernel/loader.h
 * inteiliDOS Program Loader
 *
 * Validates and executes program images that have been read into RAM.
 * Two formats are supported:
 *
 * ── IPGM (inteiliDOS native) ─────────────────────────────────────────────
 *   16-byte header followed by flat 32-bit protected-mode code and data.
 *
 *   Offset  0 – 3 : magic  "IPGM"  (0x4D475049)
 *   Offset  4 – 5 : version          1
 *   Offset  6 – 9 : entry_offset     byte offset from file start to entry fn
 *   Offset 10 –13 : load_addr        preferred physical load address
 *                                    (0 = use IPGM_LOAD_ADDR default)
 *   Offset 14 –15 : reserved         0
 *   Offset 16+    : program code and data
 *
 * ── ELF32 (i386 executable) ──────────────────────────────────────────────
 *   Standard System V ELF32 executable for the i386 architecture.
 *   loader_exec_elf() copies each PT_LOAD segment to its physical address
 *   (p_paddr), zeros the BSS (memsz > filesz), then calls e_entry.
 *
 *   The program runs in the same flat-32 protected-mode environment as the
 *   kernel: no paging, CS=0x08, DS/SS=0x10.  Virtual and physical addresses
 *   are therefore identical.  Link programs at 0x00500000 or above to stay
 *   clear of the kernel.
 *
 * Entry convention (both formats): void entry(void)
 *   Returns to LaunchPad when done.
 */

#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>

/* ── IPGM ─────────────────────────────────────────────────────────────── */

/* Four-byte magic at offset 0 of every IPGM file. */
#define IPGM_MAGIC  0x4D475049u   /* 'I','P','G','M' */

/*
 * Default physical load address for inteiliDOS programs.
 * Placed at the 5 MB mark to be well clear of the kernel + BSS.
 */
#define IPGM_LOAD_ADDR  0x00500000u

/*
 * loader_exec — attempt to execute src_buf as an inteiliDOS IPGM program.
 *
 *   src_buf  : pointer to the program bytes already in RAM
 *   size     : number of bytes
 *
 * Validates the IPGM magic and version, then calls the entry point as
 * a void function.  Returns 0 when the program returns normally, or
 * -1 if the header is invalid / magic is wrong.
 *
 * The caller is responsible for having loaded the bytes at the correct
 * physical address (load_addr field, or IPGM_LOAD_ADDR if field is 0).
 */
int loader_exec(const uint8_t *src_buf, uint32_t size);

/* ── ELF32 ────────────────────────────────────────────────────────────── */

/* ELF magic bytes. */
#define ELF_MAGIC0  0x7Fu
#define ELF_MAGIC1  'E'
#define ELF_MAGIC2  'L'
#define ELF_MAGIC3  'F'

/*
 * loader_exec_elf — load and execute a 32-bit i386 ELF executable.
 *
 *   file_buf : pointer to the raw ELF file bytes already in RAM
 *   size     : number of bytes
 *
 * Iterates over PT_LOAD program headers and copies each segment to its
 * physical address (p_paddr).  Any bytes in [p_filesz, p_memsz) are
 * zeroed (covers BSS).  Execution then transfers to e_entry.
 *
 * Return values:
 *   0   : program returned normally
 *  -1   : ELF header invalid (bad magic, class, type, or machine)
 *  -2   : segment out of bounds or inconsistent
 */
int loader_exec_elf(const uint8_t *file_buf, uint32_t size);

#endif /* LOADER_H */
