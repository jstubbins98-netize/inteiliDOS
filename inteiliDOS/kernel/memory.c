/*
 * inteilidOS -- kernel/memory.c
 * Physical memory manager (bitmap allocator) + simple kmalloc heap
 */

#include "memory.h"
#include "multiboot.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

/* ---- Kernel end symbol (from linker script) ---- */
extern uint32_t _kernel_end;

/* ---- Physical memory bitmap ---- */
#define PAGE_SIZE   4096u
#define BITMAP_SIZE (1024u * 1024u / 8u)   /* covers 4 GB if each bit = 4 KB page */

static uint8_t  phys_bitmap[BITMAP_SIZE];
static uint32_t total_mem_kb  = 0;
static uint32_t free_pages    = 0;
static uint32_t total_pages   = 0;

static void bitmap_set(uint32_t page) {
    phys_bitmap[page / 8] |= (uint8_t)(1u << (page % 8));
}
static void bitmap_clear(uint32_t page) {
    phys_bitmap[page / 8] &= (uint8_t)~(1u << (page % 8));
}
static int bitmap_test(uint32_t page) {
    return (phys_bitmap[page / 8] >> (page % 8)) & 1;
}

/* ---- Simple heap (bump allocator with free-list) ---- */
#define HEAP_SIZE   (2u * 1024u * 1024u)   /* 2 MB heap */
#define HEAP_MAGIC  0xDEADBEEFu

typedef struct heap_block {
    uint32_t magic;
    uint32_t size;      /* usable bytes */
    uint32_t free;
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

static uint8_t   heap_storage[HEAP_SIZE];
static heap_block_t *heap_head = NULL;

void heap_init(void) {
    if (heap_head != NULL) return;   /* already initialised — no-op */
    heap_head = (heap_block_t *)heap_storage;
    heap_head->magic = HEAP_MAGIC;
    heap_head->size  = HEAP_SIZE - sizeof(heap_block_t);
    heap_head->free  = 1;
    heap_head->next  = NULL;
    heap_head->prev  = NULL;
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    /* align to 8 */
    size = (size + 7u) & ~7u;

    for (heap_block_t *b = heap_head; b; b = b->next) {
        if (!b->free || b->size < size) continue;

        /* Split if there's room for another block */
        if (b->size >= size + sizeof(heap_block_t) + 8) {
            heap_block_t *nb = (heap_block_t *)((uint8_t *)b + sizeof(heap_block_t) + size);
            nb->magic = HEAP_MAGIC;
            nb->size  = b->size - size - sizeof(heap_block_t);
            nb->free  = 1;
            nb->next  = b->next;
            nb->prev  = b;
            if (b->next) b->next->prev = nb;
            b->next = nb;
            b->size = size;
        }
        b->free = 0;
        return (uint8_t *)b + sizeof(heap_block_t);
    }
    return NULL;   /* out of heap */
}

void *kcalloc(size_t nmemb, size_t size) {
    void *ptr = kmalloc(nmemb * size);
    if (ptr) kmemset(ptr, 0, nmemb * size);
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;
    heap_block_t *b = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    if (b->magic != HEAP_MAGIC) return;
    b->free = 1;

    /* Merge with next if free */
    if (b->next && b->next->free) {
        b->size += sizeof(heap_block_t) + b->next->size;
        b->next  = b->next->next;
        if (b->next) b->next->prev = b;
    }
    /* Merge with prev if free */
    if (b->prev && b->prev->free) {
        b->prev->size += sizeof(heap_block_t) + b->size;
        b->prev->next  = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

/* ---- Memory init (Multiboot1) ---- */
void memory_init(uint32_t mb_info_phys) {
    /* Mark everything used by default */
    kmemset(phys_bitmap, 0xFF, sizeof(phys_bitmap));

    multiboot_info_t *info = (multiboot_info_t *)(uintptr_t)mb_info_phys;

    /* Basic memory totals (flags bit 0) */
    if (info->flags & MULTIBOOT_FLAG_MEM) {
        total_mem_kb = info->mem_lower + info->mem_upper;
    }

    /* Full memory map (flags bit 6) */
    if (info->flags & MULTIBOOT_FLAG_MMAP) {
        uintptr_t addr = (uintptr_t)info->mmap_addr;
        uintptr_t end  = addr + info->mmap_length;

        while (addr < end) {
            multiboot_mmap_entry_t *e = (multiboot_mmap_entry_t *)addr;
            if (e->type == MULTIBOOT_MEMORY_AVAILABLE &&
                e->base_addr < 0x100000000ULL) {
                uint32_t base = (uint32_t)e->base_addr;
                uint32_t len  = (uint32_t)(e->length < 0xFFFFFFFFULL
                                           ? e->length : 0xFFFFFFFFULL);
                uint32_t page_start = (base + PAGE_SIZE - 1) / PAGE_SIZE;
                uint32_t page_end   = (base + len) / PAGE_SIZE;
                for (uint32_t p = page_start; p < page_end; p++) {
                    bitmap_clear(p);
                    free_pages++;
                    total_pages++;
                }
            }
            /* Multiboot1: advance by entry->size + 4 (size field itself) */
            addr += (uintptr_t)(e->size + 4u);
        }
    }

    /* Re-mark low memory (0–1 MB) and kernel image as used */
    uint32_t kernel_end_page = ((uint32_t)(uintptr_t)&_kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t p = 0; p < kernel_end_page; p++) bitmap_set(p);

    heap_init();
}

size_t memory_total_kb(void) { return total_mem_kb; }
size_t memory_free_kb(void)  { return (size_t)free_pages * PAGE_SIZE / 1024u; }

/* ---- String / memory helpers ---- */
void *kmemset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}
void *kmemcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}
int kmemcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    while (n--) { if (*x != *y) return (int)*x - (int)*y; x++; y++; }
    return 0;
}
size_t kstrlen(const char *s) { size_t n = 0; while (*s++) n++; return n; }
int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int kstrncmp(const char *a, const char *b, size_t n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    if (!n && *a == *b) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}
char *kstrcpy(char *dst, const char *src) {
    char *d = dst; while ((*d++ = *src++)); return dst;
}
char *kstrncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dst;
}
char *kstrcat(char *dst, const char *src) {
    char *d = dst; while (*d) d++; while ((*d++ = *src++)); return dst;
}
char *kstrtolower(char *s) {
    for (char *p = s; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
    return s;
}
const char *kstrstr(const char *hay, const char *needle) {
    size_t nl = kstrlen(needle);
    if (!nl) return hay;
    for (; *hay; hay++) if (kstrncmp(hay, needle, nl) == 0) return hay;
    return NULL;
}
