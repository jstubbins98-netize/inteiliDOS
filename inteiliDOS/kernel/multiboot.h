#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

/* Magic value the Multiboot1 bootloader puts in EAX before jumping to _start */
#define MULTIBOOT1_LOADER_MAGIC  0x2BADB002u

/* Flags in multiboot_info_t.flags */
#define MULTIBOOT_FLAG_MEM       (1u << 0)   /* mem_lower / mem_upper valid */
#define MULTIBOOT_FLAG_BOOTDEV   (1u << 1)
#define MULTIBOOT_FLAG_CMDLINE   (1u << 2)
#define MULTIBOOT_FLAG_MODS      (1u << 3)
#define MULTIBOOT_FLAG_MMAP      (1u << 6)   /* mmap_length / mmap_addr valid */

/* The Multiboot1 info structure passed by the bootloader */
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;      /* KB below 1 MB  (if MULTIBOOT_FLAG_MEM) */
    uint32_t mem_upper;      /* KB above 1 MB  (if MULTIBOOT_FLAG_MEM) */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;    /* bytes          (if MULTIBOOT_FLAG_MMAP) */
    uint32_t mmap_addr;      /* physical addr  (if MULTIBOOT_FLAG_MMAP) */
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
} __attribute__((packed)) multiboot_info_t;

/* Memory map entry.  Walk with: ptr += entry->size + 4 */
#define MULTIBOOT_MEMORY_AVAILABLE  1u
#define MULTIBOOT_MEMORY_RESERVED   2u

typedef struct {
    uint32_t size;       /* size of this entry NOT including this field */
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} __attribute__((packed)) multiboot_mmap_entry_t;

#endif /* MULTIBOOT_H */
