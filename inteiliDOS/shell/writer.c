/* inteiliWriter: bounded native word processor, with USB text/Markdown/PDF.
 * PDF is export-only; editing/loading uses ASCII TXT or Markdown documents. */
#include "writer.h"
#include "writer_document.h"
#include "writer_fonts.h"
#include "storage_dialog.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/memory.h"
#include "../kernel/fs.h"
#include "../kernel/usb_storage.h"

#define FIRST_ROW 3
#define VIEW_ROWS 19u
typedef struct {
    writer_document_t doc;
    uint8_t bytes[USB_FILE_MAX];
    writer_font_state_t fonts;
} writer_workspace_t;
static writer_workspace_t *workspace;
#define document (workspace->doc)
#define io_buffer (workspace->bytes)
static char filename[64], message[81];
static uint32_t top;

static void row_fill(unsigned row, vga_color_t fg, vga_color_t bg) {
    volatile uint16_t *vram = (volatile uint16_t *)0xB8000;
    uint16_t attr = (uint16_t)(((uint16_t)bg << 12) | (((uint16_t)fg & 7) << 8));
    for (unsigned col = 0; col < 80; col++) vram[row * 80 + col] = attr | ' ';
}
static void put(unsigned col, unsigned row, const char *text,
                 vga_color_t fg, vga_color_t bg) {
    volatile uint16_t *vram = (volatile uint16_t *)0xB8000;
    uint16_t attr = (uint16_t)(((uint16_t)bg << 12) | (((uint16_t)fg & 7) << 8));
    while (*text && col < 80) vram[row * 80 + col++] = attr | (uint8_t)*text++;
}
static void decimal(unsigned col, unsigned row, uint32_t n) {
    char s[12]; unsigned len = 0;
    do { s[len++] = (char)('0' + n % 10); n /= 10; } while (n);
    s[len] = 0;
    for (unsigned a = 0, b = len - 1; a < b; a++, b--) {
        char c = s[a]; s[a] = s[b]; s[b] = c;
    }
    put(col, row, s, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
}
static void show(const char *text) {
    kstrncpy(message, text, 80); message[80] = 0;
    row_fill(23, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    put(0, 23, message, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}
static void draw(void) {
    uint32_t row, col;
    writer_position(&document, &row, &col);
    if (row < top) top = row;
    if (row >= top + VIEW_ROWS) top = row - VIEW_ROWS + 1;
    row_fill(0, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    put(1, 0, "inteiliWriter | Word processor", VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    row_fill(1, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    put(2, 1, filename[0] ? filename : "[New document]", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    if (document.dirty) put(67, 1, "* unsaved", VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLUE);
    row_fill(2, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    put(2, 2, "Words:", VGA_COLOR_WHITE, VGA_COLOR_BLUE); decimal(9, 2, writer_words(&document));
    put(20, 2, "Chars:", VGA_COLOR_WHITE, VGA_COLOR_BLUE); decimal(27, 2, document.length);
    put(39, 2, "Row:", VGA_COLOR_WHITE, VGA_COLOR_BLUE); decimal(44, 2, row + 1);
    put(55, 2, "Col:", VGA_COLOR_WHITE, VGA_COLOR_BLUE); decimal(60, 2, col + 1);
    put(65, 2, document.typing_style & WRITER_BOLD ? "B:ON" : "B:off",
        VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    put(72, 2, document.typing_style & WRITER_ITALIC ? "I:ON" : "I:off",
        VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    for (uint32_t r = 0; r < VIEW_ROWS; r++) {
        uint32_t start, end;
        row_fill(FIRST_ROW + r, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
        if (writer_visual_line(&document, top + r, &start, &end)) {
            volatile uint16_t *vram = (volatile uint16_t *)0xB8000;
            for (uint32_t i = start; i < end; i++) {
                uint8_t glyph = (uint8_t)document.text[i];
                if (document.style[i] & WRITER_BOLD) glyph += 96;
                uint16_t attr = 0x7000 | (document.style[i] & WRITER_ITALIC ? 0x0800 : 0);
                vram[(FIRST_ROW + r) * 80 + 2 + i - start] = attr | glyph;
            }
        }
    }
    row_fill(22, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    put(1, 22, "Arrows: move | Enter: paragraph | Backspace: erase | F1: new",
        VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    show(message);
    row_fill(24, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    put(0, 24, "F2 Save F3 Load F4 Del F5 TXT F6 MD F7 PDF F8 Bold F9 Italic F10 Quit",
        VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    vga_set_cursor(FIRST_ROW + (int)(row - top), 2 + (int)col);
}
static int confirm(const char *question) {
    show(question);
    int c = keyboard_getchar();
    return c == 'y' || c == 'Y';
}
static int prompt(const char *label, char *out, unsigned capacity) {
    unsigned length = (unsigned)kstrlen(out);
    for (;;) {
        char line[81]; unsigned n = 0;
        for (unsigned i = 0; label[i] && n < 79; i++) line[n++] = label[i];
        unsigned room = 79 - n;
        unsigned start = length > room ? length - room : 0;
        for (unsigned i = start; i < length && n < 79; i++) line[n++] = out[i];
        line[n++] = '_'; line[n] = 0;
        show(line);
        int c = keyboard_getchar();
        if (c == KEY_ESCAPE) return 0;
        if (c == KEY_ENTER || c == '\n') return length != 0;
        if (c == KEY_BACKSPACE && length) out[--length] = 0;
        else if (c >= 32 && c < 127 && length + 1 < capacity) {
            out[length++] = (char)c; out[length] = 0;
        }
    }
}
static const char *extension(const char *path) {
    const char *dot = 0;
    for (; *path; path++) {
        if (*path == '\\' || *path == '/') dot = 0;
        if (*path == '.') dot = path;
    }
    return dot;
}
static int ext_is(const char *path, const char *wanted) {
    const char *ext = extension(path);
    if (!ext) return 0;
    while (*ext && *wanted) {
        char c = *ext++;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != *wanted++) return 0;
    }
    return !*ext && !*wanted;
}
static int ensure_extension(char *path, unsigned capacity, const char *ext) {
    if (extension(path)) {
        if (ext_is(path, ext)) return 1;
        show("Filename extension does not match the selected format.");
        return 0;
    }
    if (kstrlen(path) + kstrlen(ext) + 1 > capacity) {
        show("Filename is too long."); return 0;
    }
    kstrcpy(path + kstrlen(path), ext);
    return 1;
}
static void storage_error(int rc, int usb) {
    if (usb) { show(usb_storage_error(rc)); return; }
    switch (rc) {
        case FS_ERR_NODRV: show("No native HDD found; choose a mounted USB drive."); break;
        case FS_ERR_FULL: show("Native file directory is full; no file saved."); break;
        case FS_ERR_NOSPC: show("Not enough disk space; save failed."); break;
        case FS_ERR_NOTFOUND: show("File not found; document unchanged."); break;
        case FS_ERR_NODIR: case FS_ERR_LAYOUT:
            show("Native drive has no safe inteiliDOS filesystem."); break;
        default: show("Storage error; operation failed. Document retained."); break;
    }
}
static int target(char *path, int saving, int usb_only) {
    if (!usb_only) return storage_choose_target(path, 64, saving, show);
    if (usb_path(path)) return 3;
    int drive = storage_choose_usb(show);
    if (drive < 0) return 0;
    unsigned len = (unsigned)kstrlen(path);
    if (len + 7 > 64) { show("USB filename is too long."); return 0; }
    for (unsigned i = len + 1; i; i--) path[i + 5] = path[i - 1];
    path[0] = 'U'; path[1] = 'S'; path[2] = 'B';
    path[3] = (char)('0' + drive); path[4] = ':'; path[5] = '\\';
    return 3;
}
static int overwrite_ok(const char *path, int choice) {
    uint32_t length = 0; uint8_t probe;
    int rc = choice == 3 ? usb_file_read_at(path, 0, &probe, 0, &length) :
        fs_volume_read((uint8_t)(choice - 1), path, &probe, 1, &length);
    if (!rc) return confirm("File exists. Replace it? [Y] Yes  [any other key] Cancel");
    if ((choice == 3 && (rc == -4 || rc == -5)) ||
            (choice != 3 && rc == FS_ERR_NOTFOUND)) return 1;
    storage_error(rc, choice == 3);
    return 0;
}
/* format: 0=editable save; 1=USB TXT; 2=USB Markdown; 3=USB PDF. */
static void save_document(int format) {
    char path[64] = "";
    if (!format && filename[0]) kstrncpy(path, filename, 63);
    if (!prompt(format ? "USB export filename: " : "Save filename (.txt or .md): ",
                path, sizeof(path))) { show("Save/export cancelled."); return; }
    const char *ext = format == 3 ? ".pdf" : format == 2 ? ".md" :
                      (!format && (ext_is(path, ".md") ||
                       (!extension(path) && writer_has_formatting(&document)))) ? ".md" : ".txt";
    if (!ensure_extension(path, sizeof(path), ext)) return;
    if (!format && ext_is(path, ".txt") && writer_has_formatting(&document) &&
            !confirm("TXT drops bold/italic. Save plain text? [Y] Yes [other] Cancel")) {
        show("Save cancelled. Use .md to preserve bold and italic."); return;
    }
    int choice = target(path, 1, format != 0);
    if (!choice) { show("Save/export cancelled."); return; }
    uint32_t size = 0;
    int rc = format == 3 ? writer_export_pdf(&document, io_buffer, sizeof(io_buffer), &size) :
        writer_export_text(&document, ext_is(path, ".md"), io_buffer, sizeof(io_buffer), &size);
    if (rc) { show("Export exceeds the 64 KiB/128-page PDF limit. Nothing written."); return; }
    if (!overwrite_ok(path, choice)) return;
    rc = choice == 3 ? usb_file_write(path, io_buffer, size) :
        fs_volume_write((uint8_t)(choice - 1), path, io_buffer, size);
    if (rc) { storage_error(rc, choice == 3); return; }
    if (!format) {
        kstrncpy(filename, path, 63); filename[63] = 0;
        document.dirty = 0;
        show("Document saved. F3 loads TXT/Markdown; PDF is export-only.");
    } else show("USB export complete. F2 saves the editable working document.");
}
static void load_document(void) {
    if (document.dirty && !confirm("Unsaved changes. Discard after successful load? [Y] Yes"))
        { show("Load cancelled; document retained."); return; }
    char path[64] = "";
    if (!prompt("Load TXT/Markdown filename: ", path, sizeof(path))) {
        show("Load cancelled."); return;
    }
    if (!extension(path) && !ensure_extension(path, sizeof(path), ".txt")) return;
    if (!ext_is(path, ".txt") && !ext_is(path, ".md")) {
        show("Load supports TXT/Markdown only, not PDF or binary documents."); return;
    }
    int choice = target(path, 0, 0);
    if (!choice) { show("Load cancelled."); return; }
    uint32_t size = 0;
    int rc = choice == 3 ? usb_file_read(path, io_buffer, sizeof(io_buffer), &size) :
        fs_volume_read((uint8_t)(choice - 1), path, io_buffer, sizeof(io_buffer), &size);
    if (rc) { storage_error(rc, choice == 3); return; }
    int invalid = ext_is(path, ".md") ?
        writer_document_load_markdown(&document, io_buffer, size) :
        writer_document_load(&document, io_buffer, size);
    if (invalid) {
        show("Not loaded: ASCII text only, up to 16 KiB. Original document retained."); return;
    }
    kstrncpy(filename, path, 63); filename[63] = 0; top = 0;
    show("Document loaded. Tabs expanded to spaces; CRLF normalized.");
}
void writer_run(void) {
    workspace = kmalloc(sizeof(*workspace));
    if (!workspace) {
        vga_puts("inteiliWriter: not enough memory. Press any key.\n");
        keyboard_getchar();
        return;
    }
    writer_fonts_begin(&workspace->fonts);
    writer_document_init(&document);
    filename[0] = 0; top = 0;
    show("Ready. USB exports require USB /MOUNT first. F5 TXT / F6 MD / F7 PDF.");
    for (;;) {
        draw();
        int c = keyboard_getchar();
        if (c == KEY_F10) {
            if (!document.dirty || confirm("Unsaved changes. Quit without saving? [Y] Yes")) break;
            show("Quit cancelled.");
        } else if (c == KEY_F1) {
            if (!document.dirty || confirm("Discard changes and create a new document? [Y] Yes")) {
                writer_document_init(&document); filename[0] = 0; top = 0; show("New document.");
            }
        } else if (c == KEY_F2) save_document(0);
        else if (c == KEY_F3) load_document();
        else if (c == KEY_F4) { writer_delete_line(&document); show("Logical line deleted."); }
        else if (c == KEY_F5) save_document(1);
        else if (c == KEY_F6) save_document(2);
        else if (c == KEY_F7) save_document(3);
        else if (c == KEY_F8) {
            document.typing_style ^= WRITER_BOLD;
            show(document.typing_style & WRITER_BOLD ? "Bold ON for new text." : "Bold OFF for new text.");
        }
        else if (c == KEY_F9) {
            document.typing_style ^= WRITER_ITALIC;
            show(document.typing_style & WRITER_ITALIC ? "Italic ON for new text." : "Italic OFF for new text.");
        }
        else if (c == KEY_LEFT && document.cursor) document.cursor--;
        else if (c == KEY_RIGHT && document.cursor < document.length) document.cursor++;
        else if (c == KEY_UP) writer_vertical(&document, -1);
        else if (c == KEY_DOWN) writer_vertical(&document, 1);
        else if (c == KEY_BACKSPACE) writer_backspace(&document);
        else if (c == '\t') {
            if (document.length + 4 > WRITER_CAPACITY) show("No room for a four-space tab.");
            else for (unsigned i = 0; i < 4; i++) writer_insert(&document, ' ');
        }
        else if (c == KEY_ENTER || c == '\n' || (c >= 32 && c < 127)) {
            char value = c == KEY_ENTER ? '\n' : (char)c;
            if (writer_insert(&document, value)) show("Document full (16 KiB); save or export first.");
        }
    }
    writer_fonts_end(&workspace->fonts);
    vga_clear();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kfree(workspace);
    workspace = 0;
}
