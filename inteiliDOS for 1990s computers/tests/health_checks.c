/* Focused, real i386 kernel tests; private state is changed only in this
 * disposable fixture, never in a production build. */
#include "../kernel/memory.c"
#include "../shell/health.c"

int cdrom_count(void) { return 0; }
uint32_t timer_get_ticks(void) { return 1000; }
static void log(const char *s) {
    while (*s) { __asm__ volatile("outb %0,$0xE9" :: "a"((uint8_t)*s)); s++; }
}
static void finish(int pass) {
    __asm__ volatile("outl %0,$0xF4" :: "a"(pass ? 0x10u : 0x11u));
    for (;;) __asm__ volatile("hlt");
}
#define CHECK(expr) do { if (!(expr)) { log("FAIL: " #expr "\n"); finish(0); } } while (0)
static int screen_has(const char *text) {
    volatile uint16_t *screen=(volatile uint16_t *)0xB8000;
    for (unsigned i=0; i<2000; i++) {
        unsigned j=0;
        while (text[j] && i+j<2000 && (char)screen[i+j]==text[j]) j++;
        if (!text[j]) return 1;
    }
    return 0;
}
static void checksum(uint8_t *data) {
    unsigned sum=0;
    for (unsigned i=0; i<511; i++) sum+=data[i];
    data[511]=(uint8_t)(0-sum);
}
void test_main(void) {
    heap_init();
    memory_heap_stats_t original, allocated, released;
    CHECK(memory_heap_snapshot(&original)==0);
    void *p=kmalloc(4096); CHECK(p!=NULL);
    CHECK(memory_heap_snapshot(&allocated)==0 && allocated.used_bytes>=4096);
    kfree(p);
    CHECK(memory_heap_snapshot(&released)==0 && released.free_bytes==original.free_bytes);
    heap_block_t *saved=heap_head->next;
    heap_head->next=(heap_block_t *)1;
    CHECK(memory_heap_snapshot(&released)<0); /* reject without dereferencing */
    heap_head->next=heap_head;
    CHECK(memory_heap_snapshot(&released)<0); /* reject cycle */
    heap_head->next=saved;
    uint32_t size=heap_head->size;
    heap_head->size=0xFFFFFFFFu;
    CHECK(memory_heap_snapshot(&released)<0);
    heap_head->size=size;

    uint8_t data[512]={1};
    ata_health_t smart={0};
    data[2]=5; data[7]=3;
    data[14]=197; data[19]=2;
    data[26]=198; data[31]=1;
    data[38]=194; data[43]=65;
    checksum(data);
    CHECK(ata_parse_smart(data,&smart)==0);
    CHECK(smart.attributes_valid && smart.reallocated==3 && smart.pending==2 &&
          smart.uncorrectable==1 && smart.temperature_valid && smart.temperature_c==65);
    data[511]^=1;
    CHECK(ata_parse_smart(data,&smart)<0 && !smart.attributes_valid && !smart.temperature_valid);
    data[511]^=1;
    data[38]=0; data[7]=0; data[11]=1; checksum(data);
    CHECK(ata_parse_smart(data,&smart)==0 && smart.reallocated==0xFFFFFFFFu &&
          !smart.temperature_valid);
    kmemset(data,0,512);
    CHECK(ata_parse_smart(data,&smart)<0);

    vga_init(); total_mem_kb=16384; free_pages=1024;
    count=0; page=0; draw(1000);
    CHECK(screen_has("No problems detected") && screen_has("No ATA HDD detected"));
    page=1; draw(1000); CHECK(screen_has("No ATA hard disk detected"));
    count=1; selected=0;
    disks[0]=(disk_t){.first_ok=1,.last_ok=1,.smart={.status=ATA_HEALTH_OK,.attributes_valid=1}};
    draw(1000); CHECK(screen_has("Drive reports PASS"));
    disks[0].smart.status=ATA_HEALTH_DISABLED;
    draw(1000); CHECK(screen_has("WARNINGS / LIMITED") && screen_has("Disabled (left unchanged)"));
    disks[0].smart.status=ATA_HEALTH_INTERFACE;
    draw(1000); CHECK(screen_has("Unavailable through this AHCI driver"));
    disks[0].smart.status=ATA_HEALTH_FAIL;
    draw(1000); CHECK(screen_has("PROBLEM DETECTED") && screen_has("replace the drive"));
    disks[0].smart.status=ATA_HEALTH_OK;
    disks[0].smart.pending_valid=1; disks[0].smart.pending=2;
    draw(1000); CHECK(screen_has("WARNINGS / LIMITED") && screen_has("Unstable/unreadable sectors"));
    disks[0].smart.pending=0;
    disks[0].smart.temperature_valid=1; disks[0].smart.temperature_c=65;
    draw(1000); CHECK(screen_has("inspect drive cooling"));
    disks[0].smart.temperature_valid=0; disks[0].first_ok=0;
    draw(1000); CHECK(screen_has("PROBLEM DETECTED") && screen_has("READ ERROR"));
    page=0; count=0; heap_head->magic=0;
    draw(1000); CHECK(screen_has("PROBLEM DETECTED") && screen_has("Heap error"));
    heap_head->magic=HEAP_MAGIC; free_pages=1;
    draw(1000); CHECK(screen_has("Low allocatable memory"));
    log("PASS: SMART validation, heap bounds, alerts and unavailable monitoring\n");
    finish(1);
}
