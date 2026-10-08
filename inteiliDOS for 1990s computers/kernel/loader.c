/*
 * inteiliDOS -- kernel/loader.c
 * inteiliDOS Program Loader
 *
 * Validates program images in RAM and transfers control to their entry
 * points.  Two formats are handled:
 *   • IPGM — inteiliDOS native flat binary with a 16-byte header
 *   • ELF32 — standard i386 ELF executable (ET_EXEC, EM_386)
 */

#include "loader.h"
#include <stdint.h>

/* =========================================================================
 * IPGM loader
 * ========================================================================= */
int loader_exec(const uint8_t *src_buf, uint32_t size) {
    if (!src_buf || size < 16u)
        return -1;

    /* Validate magic. */
    uint32_t magic =  (uint32_t)src_buf[0]
                   | ((uint32_t)src_buf[1] <<  8)
                   | ((uint32_t)src_buf[2] << 16)
                   | ((uint32_t)src_buf[3] << 24);
    if (magic != IPGM_MAGIC)
        return -1;

    /* Version check: we only understand version 1. */
    uint16_t ver = (uint16_t)(src_buf[4] | ((uint16_t)src_buf[5] << 8));
    if (ver != 1u)
        return -1;

    /* Compute entry address. */
    uint32_t entry_off  = (uint32_t)src_buf[6]
                        | ((uint32_t)src_buf[7]  <<  8)
                        | ((uint32_t)src_buf[8]  << 16)
                        | ((uint32_t)src_buf[9]  << 24);
    uint32_t load_addr  = (uint32_t)src_buf[10]
                        | ((uint32_t)src_buf[11] <<  8)
                        | ((uint32_t)src_buf[12] << 16)
                        | ((uint32_t)src_buf[13] << 24);

    if (load_addr == 0u)
        load_addr = IPGM_LOAD_ADDR;

    if (entry_off >= size)
        return -1;  /* entry point beyond end of file */

    uintptr_t entry_phys = (uintptr_t)(load_addr + entry_off);

    /*
     * Cast the physical entry address to a function pointer and call it.
     * The program runs in the same flat-32 protected-mode environment as
     * the kernel (CS=0x08, DS/SS=0x10, no paging).  It returns here when
     * it exits.
     */
    typedef void (*entry_fn_t)(void);
    entry_fn_t entry = (entry_fn_t)(void *)entry_phys;
    entry();

    return 0;
}

/* =========================================================================
 * ELF32 loader
 * ========================================================================= */

/* Byte-safe little-endian reads from a buffer at a given offset. */
static inline uint16_t elf_r16(const uint8_t *p, uint32_t off) {
    return (uint16_t)(p[off] | ((uint16_t)p[off + 1] << 8));
}
static inline uint32_t elf_r32(const uint8_t *p, uint32_t off) {
    return  (uint32_t)p[off]
         | ((uint32_t)p[off + 1] <<  8)
         | ((uint32_t)p[off + 2] << 16)
         | ((uint32_t)p[off + 3] << 24);
}

/*
 * ELF32 header field offsets (SYSV ABI, byte positions in the header).
 * All values are read as little-endian because we only support EM_386.
 */
#define ELF32_E_CLASS     4    /* 1 byte  — 1 = ELFCLASS32           */
#define ELF32_E_TYPE     16    /* 2 bytes — 2 = ET_EXEC              */
#define ELF32_E_MACHINE  18    /* 2 bytes — 3 = EM_386               */
#define ELF32_E_ENTRY    24    /* 4 bytes — virtual entry point      */
#define ELF32_E_PHOFF    28    /* 4 bytes — program header offset    */
#define ELF32_E_PHESZ    42    /* 2 bytes — program header entry sz  */
#define ELF32_E_PHNUM    44    /* 2 bytes — number of prog headers   */
#define ELF32_HDR_MIN    52    /* minimum ELF32 header size          */

/*
 * ELF32 program header field offsets (within each phdr entry).
 * PT_LOAD = 1.
 */
#define ELF32_PH_TYPE    0     /* 4 bytes — segment type  */
#define ELF32_PH_OFFSET  4     /* 4 bytes — file offset   */
#define ELF32_PH_VADDR   8     /* 4 bytes — virtual addr  */
#define ELF32_PH_PADDR  12     /* 4 bytes — physical addr */
#define ELF32_PH_FILESZ 16     /* 4 bytes — file size     */
#define ELF32_PH_MEMSZ  20     /* 4 bytes — memory size   */
#define ELF32_PH_MIN    32     /* minimum phdr entry size */

int loader_exec_elf(const uint8_t *file_buf, uint32_t size) {
    /* ── Validate ELF32 header ─────────────────────────────────────────── */
    if (!file_buf || size < ELF32_HDR_MIN)
        return -1;

    /* Magic: \x7f E L F */
    if (file_buf[0] != ELF_MAGIC0 || file_buf[1] != ELF_MAGIC1 ||
        file_buf[2] != ELF_MAGIC2 || file_buf[3] != ELF_MAGIC3)
        return -1;

    /* Must be 32-bit class */
    if (file_buf[ELF32_E_CLASS] != 1)
        return -1;

    /* Must be an executable (ET_EXEC = 2) */
    if (elf_r16(file_buf, ELF32_E_TYPE) != 2)
        return -1;

    /* Must target i386 (EM_386 = 3) */
    if (elf_r16(file_buf, ELF32_E_MACHINE) != 3)
        return -1;

    uint32_t entry   = elf_r32(file_buf, ELF32_E_ENTRY);
    uint32_t phoff   = elf_r32(file_buf, ELF32_E_PHOFF);
    uint16_t phesz   = elf_r16(file_buf, ELF32_E_PHESZ);
    uint16_t phnum   = elf_r16(file_buf, ELF32_E_PHNUM);

    if (phoff >= size || phesz < ELF32_PH_MIN || phnum == 0)
        return -1;

    /* ── Load each PT_LOAD segment ─────────────────────────────────────── */
    for (uint16_t i = 0; i < phnum; i++) {
        uint32_t ph_off = phoff + (uint32_t)i * phesz;
        if (ph_off + ELF32_PH_MIN > size)
            return -2;   /* program header beyond file */

        const uint8_t *ph = file_buf + ph_off;
        if (elf_r32(ph, ELF32_PH_TYPE) != 1)
            continue;    /* not PT_LOAD — skip */

        uint32_t p_offset = elf_r32(ph, ELF32_PH_OFFSET);
        uint32_t p_paddr  = elf_r32(ph, ELF32_PH_PADDR);
        uint32_t p_filesz = elf_r32(ph, ELF32_PH_FILESZ);
        uint32_t p_memsz  = elf_r32(ph, ELF32_PH_MEMSZ);

        /* Sanity: file portion must fit in the file and in memory alloc */
        if (p_filesz > p_memsz)
            return -2;
        if (p_filesz > 0 && p_offset + p_filesz > size)
            return -2;

        /* Copy file bytes to physical address (no paging — vaddr == paddr). */
        uint8_t       *dst = (uint8_t *)(uintptr_t)p_paddr;
        const uint8_t *src = file_buf + p_offset;
        for (uint32_t j = 0; j < p_filesz; j++)
            dst[j] = src[j];

        /* Zero the BSS tail: bytes in [filesz, memsz). */
        for (uint32_t j = p_filesz; j < p_memsz; j++)
            dst[j] = 0;
    }

    /* ── Transfer control to e_entry ───────────────────────────────────── */
    typedef void (*entry_fn_t)(void);
    entry_fn_t fn = (entry_fn_t)(void *)(uintptr_t)entry;
    fn();
    return 0;
}
