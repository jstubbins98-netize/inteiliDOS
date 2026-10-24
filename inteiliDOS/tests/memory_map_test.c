/* Actual physical allocator with bounded low-address host fixtures. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "../kernel/memory.c"

uint32_t _kernel_end;
static multiboot_info_t info;
static multiboot_mmap_entry_t map[8];
static unsigned warnings;
void vga_puts(const char *s) {(void)s;++warnings;}

static void entry(unsigned i,uint64_t base,uint64_t length,unsigned type) {
    map[i]=(multiboot_mmap_entry_t){20,base,length,type};
}
static void init(unsigned count) {
    assert((uintptr_t)&info<=UINT32_MAX && (uintptr_t)map<=UINT32_MAX);
    memset(&info,0,sizeof(info));
    info.flags=MULTIBOOT_FLAG_MMAP;
    info.mmap_addr=(uint32_t)(uintptr_t)map;
    info.mmap_length=count*sizeof(*map);
    memory_init((uint32_t)(uintptr_t)&info);
}
int main(void) {
    heap_init();
    entry(0,0,64u*1024u*1024u,1);
    entry(1,32u*1024u*1024u,4u*1024u*1024u,2);
    entry(2,16u*1024u*1024u,32u*1024u*1024u,1);
    init(3);
    assert(memory_total_kb()==64u*1024u);
    assert(bitmap_test(32u*1024u*1024u/PAGE_SIZE));
    assert(!bitmap_test(36u*1024u*1024u/PAGE_SIZE));
    /* Actual zeroing allocation, not just bitmap inspection. */
    void *window=mmap((void *)0x1000000,16u*1024u*1024u,
        PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(window==(void *)0x1000000);
    memset(window,0xa5,16u*1024u*1024u);
    assert(memory_alloc_pages(4096)==window);
    assert(!((uint8_t *)window)[0] && !((uint8_t *)window)[16u*1024u*1024u-1]);
    memory_free_pages(window,4096);
    munmap(window,16u*1024u*1024u);
    entry(0,0,16u*1024u*1024u,1);init(1);
    assert(memory_alloc_pages(4096)==NULL); /* genuinely insufficient RAM */
    /* Reserved overlaps win, including single bytes inside a RAM page. */
    entry(0,16u*1024u*1024u,4u*PAGE_SIZE,1);
    entry(1,16u*1024u*1024u+PAGE_SIZE+1u,1,2);init(2);
    assert(!bitmap_test(4096) && bitmap_test(4097) && !bitmap_test(4098));
    entry(0,0xffffe800ULL,0x100000000ULL,1);init(1);
    assert(memory_total_kb()==4194304u);
    assert(bitmap_test(1048574) && !bitmap_test(1048575));
    entry(0,0x100000000ULL,0x1000000,1);init(1);
    assert(memory_free_kb()==0); /* above IA-32 addressability */
    entry(0,0,64u*1024u*1024u,1);
    entry(1,0,1,1);map[1].size=UINT32_MAX;init(2);
    assert(!memory_free_kb() && !memory_total_kb() && warnings);
    map[0].size=0;init(1);assert(!memory_free_kb());
    init(0);assert(!memory_free_kb());
    info.mmap_addr=0xfffffff0u;info.mmap_length=32;
    memory_init((uint32_t)(uintptr_t)&info);assert(!memory_free_kb());
    memset(&info,0,sizeof(info));
    memory_init((uint32_t)(uintptr_t)&info);
    assert(!memory_alloc_pages(4096)); /* no guessed RAM from config */
    puts("PASS: BIOS mmap allocation, reserved overlaps, duplicates, bounds, corrupt/absent maps and real low-RAM refusal");
}
