/* Read-only DOS file bridge to the same ISO9660/FAT12 drivers as LaunchPad.
 * Files are not copied from another medium or from host-side mock data. */
#include "launchpad_dos.h"
#include "../kernel/iso9660.h"
#include "../kernel/fat12.h"
#include "../kernel/loader.h"
#include <stddef.h>

#define MAX_READ (4u*1024u*1024u)
static int source;
static char base[272];
static struct {
    iso9660_dirent_t cd;
    fat12_dirent_t fat;
} handles[8];
static int upper(int c) { return c >= 'a' && c <= 'z' ? c-32 : c; }
static int same(const char *a, const char *b) {
    while (*a && *b) if (upper(*a++) != upper(*b++)) return 0;
    return !*a && !*b;
}
static int slash(char c) { return c == '/' || c == '\\'; }

/* Expand a guest path underneath the selected program's directory.
 * ".." cannot escape this per-session root. Drive letters cannot change
 * physical media behind LaunchPad's selected source. */
static int resolve(const char *guest, char path[544]) {
    if (guest[0] && guest[1] == ':') {
        if (upper(guest[0]) != (source < 4 ? 'D' : 'A')) return 15;
        guest += 2;
    }
    unsigned n = 0;
    while (base[n]) { path[n] = base[n]; n++; }
    while (n && path[n-1] == '/') n--;
    unsigned floor = n;
    while (*guest) {
        while (slash(*guest)) guest++;
        if (!*guest) break;
        const char *part = guest;
        unsigned len = 0;
        while (guest[len] && !slash(guest[len])) len++;
        guest += len;
        if (len == 1 && part[0] == '.') continue;
        if (len == 2 && part[0] == '.' && part[1] == '.') {
            if (n <= floor) return 3;
            while (n > floor && path[n-1] != '/') n--;
            if (n > floor) n--;
            continue;
        }
        if (!len || len >= ISO9660_NAME_MAX || n+len+2 >= 544) return 3;
        path[n++] = '/';
        for (unsigned i = 0; i < len; i++) {
            if (part[i] == ':' || part[i] == '*' || part[i] == '?') return 3;
            path[n++] = part[i];
        }
    }
    path[n] = 0;
    return n > floor ? 0 : 2;
}
static int open_file(const char *guest, unsigned slot, uint32_t *size) {
    char path[544], component[ISO9660_NAME_MAX];
    if (slot >= 8) return 6;
    int e = resolve(guest, path);
    if (e) return e;
    unsigned pos = 0;
    uint32_t lba = 0, dir_size = 0;
    uint16_t cluster = 0;
    while (path[pos]) {
        while (path[pos] == '/') pos++;
        unsigned n = 0;
        while (path[pos] && path[pos] != '/') component[n++] = path[pos++];
        component[n] = 0;
        if (!n) break;
        if (source < 4) {
            iso9660_dirent_t list[ISO9660_MAX_FILES];
            int count = lba ? iso9660_read_dir_at(source, lba, dir_size, list)
                            : iso9660_read_dir(source, list);
            if (count < 0) return 30;
            int found = -1;
            for (int i = 0; i < count; i++) if (same(component, list[i].name)) { found = i; break; }
            if (found < 0) return path[pos] ? 3 : 2;
            if (path[pos]) {
                if (!list[found].is_dir) return 3;
                lba = list[found].lba; dir_size = list[found].size;
            } else {
                if (list[found].is_dir || list[found].size > MAX_READ) return 5;
                handles[slot].cd = list[found]; *size = list[found].size; return 0;
            }
        } else {
            fat12_dirent_t list[FAT12_MAX_FILES];
            int count = cluster ? fat12_read_subdir(0, cluster, list) : fat12_read_dir(0, list);
            if (count < 0) return 30;
            int found = -1;
            for (int i = 0; i < count; i++) if (same(component, list[i].name)) { found = i; break; }
            if (found < 0) return path[pos] ? 3 : 2;
            if (path[pos]) {
                if (!(list[found].attr & FAT12_ATTR_DIRECTORY)) return 3;
                cluster = list[found].first_cluster;
                if (cluster < 2) return 3;
            } else {
                if ((list[found].attr & FAT12_ATTR_DIRECTORY) || list[found].size > MAX_READ) return 5;
                handles[slot].fat = list[found]; *size = list[found].size; return 0;
            }
        }
    }
    return 2;
}
static int32_t read_file(unsigned slot, uint32_t offset, uint8_t *out, uint32_t count) {
    if (slot >= 8 || offset > MAX_READ || count > MAX_READ-offset) return -5;
    uint8_t *scratch = (uint8_t *)(uintptr_t)IPGM_LOAD_ADDR;
    /* Existing drivers read prefixes. This is deliberately bounded and slower
     * than a sector cache, but uses actual medium contents for every read. */
    uint32_t prefix = offset+count;
    int32_t bytes = source < 4 ? iso9660_read_file(source, &handles[slot].cd, scratch, prefix)
                              : fat12_read_file(0, &handles[slot].fat, scratch, prefix);
    if (bytes < 0 || (uint32_t)bytes < prefix) return -30;
    for (uint32_t i = 0; i < count; i++) out[i] = scratch[offset+i];
    return count;
}
const dos_filesystem_t *launchpad_dos_source(int src, const char *directory) {
    static dos_filesystem_t fs = {open_file, read_file, 'D'};
    source = src;
    unsigned i = 0;
    while (directory[i] && i+1 < sizeof(base)) { base[i] = directory[i]; i++; }
    base[i] = 0;
    fs.drive = src < 4 ? 'D' : 'A';
    return &fs;
}
