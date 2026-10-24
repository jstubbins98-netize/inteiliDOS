/* Host-only UI/storage fixture. Does not touch any disk or actual VGA device. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../kernel/usb_storage.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../shell/writer_fonts.h"

typedef struct { char path[64]; uint8_t data[65536]; uint32_t size; int used; } file_t;
static file_t files[16];
static int keys[65536], key_count, key_at, exhausted, write_fail, quit_confirmations;
static usb_volume_info_t volumes[USB_STORAGE_MAX];
static char last_screen[2001];
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr) { free(ptr); }
void vga_puts(const char *text) { (void)text; }
void writer_fonts_begin(writer_font_state_t *state) { (void)state; }
void writer_fonts_end(const writer_font_state_t *state) { (void)state; }

size_t kstrlen(const char *s) { return strlen(s); }
char *kstrcpy(char *out, const char *s) { return strcpy(out, s); }
char *kstrncpy(char *out, const char *s, size_t n) { return strncpy(out, s, n); }
int writer_host_reset(void) {
    void *p = mmap((void *)0xB8000, 4096, PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) return -1;
    memset(files, 0, sizeof(files)); memset(volumes, 0, sizeof(volumes));
    volumes[0].mounted = volumes[1].mounted = 1;
    key_count = key_at = exhausted = write_fail = quit_confirmations = 0;
    return 0;
}
void writer_host_keys(const int *input, int count) {
    memcpy(keys, input, (size_t)count * sizeof(int)); key_count = count; key_at = 0;
}
int writer_host_exhausted(void) { return exhausted; }
void writer_host_fail(int fail) { write_fail = fail; }
int keyboard_getchar(void) {
    if (key_at < key_count) return keys[key_at++];
    exhausted++;
    /* Bound a broken test's loop, without hiding its queue exhaustion. */
    return exhausted % 2 ? KEY_F10 : 'y';
}
void vga_set_cursor(int row, int col) { (void)row; (void)col; }
void vga_set_color(vga_color_t fg, vga_color_t bg) { (void)fg; (void)bg; }
void vga_clear(void) {
    volatile uint16_t *vram = (volatile uint16_t *)0xB8000;
    for (unsigned i = 0; i < 2000; i++) last_screen[i] = vram[i] & 255;
    last_screen[2000] = 0;
}
const char *writer_host_screen(void) { return last_screen; }
static file_t *find(const char *path, int create) {
    for (unsigned i = 0; i < 16; i++)
        if (files[i].used && !strcmp(files[i].path, path)) return &files[i];
    if (create) for (unsigned i = 0; i < 16; i++) if (!files[i].used) {
        files[i].used = 1; strncpy(files[i].path, path, 63); return &files[i];
    }
    return 0;
}
int writer_host_store(const char *path, const uint8_t *data, uint32_t size) {
    file_t *file = find(path, 1);
    if (!file || size > sizeof(file->data)) return -1;
    memcpy(file->data, data, size); file->size = size; return 0;
}
int writer_host_fetch(const char *path, uint8_t *out) {
    file_t *file = find(path, 0);
    if (!file) return -1;
    memcpy(out, file->data, file->size); return (int)file->size;
}
int usb_path(const char *path) {
    return (path[0] == 'U' || path[0] == 'u') &&
           (path[1] == 'S' || path[1] == 's') &&
           (path[2] == 'B' || path[2] == 'b');
}
const usb_volume_info_t *usb_storage_volumes(void) { return volumes; }
const char *usb_storage_error(int code) { (void)code; return "USB operation failed; document retained."; }
int usb_file_write(const char *path, const uint8_t *data, uint32_t size) {
    return write_fail ? -1 : writer_host_store(path, data, size);
}
int usb_file_read(const char *path, uint8_t *out, uint32_t max, uint32_t *size) {
    file_t *file = find(path, 0);
    if (!file) return -4;
    if (file->size > max) return -102;
    memcpy(out, file->data, file->size); *size = file->size; return 0;
}
int usb_file_read_at(const char *path, uint32_t offset, uint8_t *out,
                     uint32_t count, uint32_t *size) {
    file_t *file = find(path, 0);
    if (!file) return -4;
    if (offset > file->size || count > file->size - offset) return -102;
    memcpy(out, file->data + offset, count); *size = file->size; return 0;
}
static void native_name(uint8_t volume, const char *name, char path[64]) {
    path[0] = (char)('C' + volume); path[1] = ':'; path[2] = '\\';
    strncpy(path + 3, name, 60); path[63] = 0;
}
int fs_volume_read(uint8_t volume, const char *name, uint8_t *out,
                   uint32_t max, uint32_t *size) {
    char path[64]; native_name(volume, name, path);
    file_t *file = find(path, 0);
    if (!file) return -5;
    *size = file->size < max ? file->size : max;
    memcpy(out, file->data, *size); return 0;
}
int fs_volume_write(uint8_t volume, const char *name, const uint8_t *data, uint32_t size) {
    char path[64]; native_name(volume, name, path);
    return write_fail ? -3 : writer_host_store(path, data, size);
}
