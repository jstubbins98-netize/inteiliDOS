#include "writer_document.h"

void writer_document_init(writer_document_t *doc) {
    doc->length = doc->cursor = 0;
    doc->dirty = 0;
    doc->typing_style = 0;
}

static unsigned marker_mask(unsigned count) {
    return count == 1 ? WRITER_ITALIC : (count == 2 || count == 4) ? WRITER_BOLD :
           count == 3 ? WRITER_BOLD | WRITER_ITALIC : 0;
}
/* Count remaining emphasis delimiters in one line, ignoring escaped stars.
 * Parsing stays linear even with many emphasis runs in a large document. */
static void markers(const uint8_t *data, uint32_t size, uint32_t at,
                    unsigned *bold, unsigned *italic) {
    *bold = *italic = 0;
    while (at < size && data[at] != '\n' && data[at] != '\r') {
        if (data[at] == '\\' && at + 1 < size &&
                (data[at + 1] == '*' || data[at + 1] == '\\')) at += 2;
        else if (data[at] == '*') {
            unsigned count = 0;
            while (at < size && data[at] == '*') { count++; at++; }
            unsigned mask = marker_mask(count);
            *bold += !!(mask & WRITER_BOLD);
            *italic += !!(mask & WRITER_ITALIC);
        } else at++;
    }
}
static int load_pass(writer_document_t *doc, const uint8_t *data, uint32_t size,
                     int markdown, int apply) {
    uint32_t n = 0;
    unsigned style = 0, bold = 0, italic = 0;
    if (markdown) markers(data, size, 0, &bold, &italic);
    for (uint32_t i = 0; i < size;) {
        uint8_t c = data[i++];
        unsigned repeat = 1;
        if (c == '\r' && i < size && data[i] == '\n') continue;
        if (c != '\r' && c != '\n' && c != '\t' && (c < 32 || c > 126)) return -1;
        if (markdown && c == '\\' && i < size && (data[i] == '*' || data[i] == '\\'))
            c = data[i++];
        else if (markdown && c == '*') {
            repeat = 1;
            while (i < size && data[i] == '*') { i++; repeat++; }
            unsigned mask = marker_mask(repeat);
            bold -= !!(mask & WRITER_BOLD);
            italic -= !!(mask & WRITER_ITALIC);
            unsigned opening = mask & ~style;
            if (mask && (!(opening & WRITER_BOLD) || bold) &&
                        (!(opening & WRITER_ITALIC) || italic)) {
                style ^= mask;
                continue;
            }
        }
        if (c == '\r' || c == '\n') {
            c = '\n'; style = 0;
            if (markdown) markers(data, size, i, &bold, &italic);
        } else if (c == '\t') { c = ' '; repeat = 4; }
        if (repeat > WRITER_CAPACITY - n) return -1;
        if (apply) for (unsigned j = 0; j < repeat; j++) {
            doc->text[n + j] = (char)c;
            doc->style[n + j] = (uint8_t)style;
        }
        n += repeat;
    }
    if (apply) {
        doc->length = n; doc->cursor = 0; doc->dirty = 0; doc->typing_style = 0;
    }
    return 0;
}
static int load_document(writer_document_t *doc, const uint8_t *data,
                         uint32_t size, int markdown) {
    if (load_pass(doc, data, size, markdown, 0)) return -1;
    return load_pass(doc, data, size, markdown, 1);
}
int writer_document_load(writer_document_t *doc, const uint8_t *data, uint32_t size) {
    return load_document(doc, data, size, 0);
}
int writer_document_load_markdown(writer_document_t *doc, const uint8_t *data, uint32_t size) {
    return load_document(doc, data, size, 1);
}
int writer_has_formatting(const writer_document_t *doc) {
    for (uint32_t i = 0; i < doc->length; i++)
        if (doc->style[i] && doc->text[i] != ' ' && doc->text[i] != '\n') return 1;
    return 0;
}

int writer_insert(writer_document_t *doc, char c) {
    if (doc->length == WRITER_CAPACITY) return -1;
    for (uint32_t i = doc->length; i > doc->cursor; i--) {
        doc->text[i] = doc->text[i - 1]; doc->style[i] = doc->style[i - 1];
    }
    doc->text[doc->cursor] = c;
    doc->style[doc->cursor++] = doc->typing_style;
    doc->length++;
    doc->dirty = 1;
    return 0;
}

void writer_backspace(writer_document_t *doc) {
    if (!doc->cursor) return;
    doc->cursor--;
    for (uint32_t i = doc->cursor; i + 1 < doc->length; i++) {
        doc->text[i] = doc->text[i + 1]; doc->style[i] = doc->style[i + 1];
    }
    doc->length--;
    doc->dirty = 1;
}

void writer_delete_line(writer_document_t *doc) {
    uint32_t start = doc->cursor, end = doc->cursor;
    while (start && doc->text[start - 1] != '\n') start--;
    while (end < doc->length && doc->text[end] != '\n') end++;
    if (end < doc->length) end++;
    else if (start) start--;
    if (end == start) return;
    for (uint32_t i = end; i < doc->length; i++) {
        doc->text[start + i - end] = doc->text[i];
        doc->style[start + i - end] = doc->style[i];
    }
    doc->length -= end - start;
    doc->cursor = start;
    doc->dirty = 1;
}

/* Soft wrapping never changes the stored paragraph/newline structure. */
static uint32_t line_end(const writer_document_t *doc, uint32_t start) {
    uint32_t end = start;
    while (end < doc->length && end - start < WRITER_WIDTH && doc->text[end] != '\n') end++;
    /* Prefer a word boundary; preserve the separating space in the output. */
    if (end < doc->length && doc->text[end] != '\n') {
        uint32_t word = end;
        while (word > start && doc->text[word - 1] != ' ') word--;
        if (word > start) end = word;
    }
    return end;
}

int writer_visual_line(const writer_document_t *doc, uint32_t row,
                       uint32_t *start, uint32_t *end) {
    uint32_t at = 0;
    for (uint32_t r = 0;; r++) {
        uint32_t stop = line_end(doc, at);
        if (r == row) { *start = at; *end = stop; return 1; }
        if (stop == doc->length) return 0;
        at = stop + (doc->text[stop] == '\n');
    }
}

void writer_position(const writer_document_t *doc, uint32_t *row, uint32_t *col) {
    uint32_t at = 0, r = 0;
    for (;;) {
        uint32_t end = line_end(doc, at);
        if (end == doc->length || doc->cursor < end ||
                (doc->cursor == end && doc->text[end] == '\n')) {
            *row = r; *col = doc->cursor - at; return;
        }
        at = end + (doc->text[end] == '\n');
        r++;
    }
}

void writer_vertical(writer_document_t *doc, int direction) {
    uint32_t row, col, start, end;
    writer_position(doc, &row, &col);
    if (direction < 0 && !row) return;
    uint32_t target = direction < 0 ? row - 1 : row + 1;
    if (writer_visual_line(doc, target, &start, &end)) {
        uint32_t last = end - start;
        /* At a soft-wrap boundary, offset=end belongs to the next row. */
        if (end < doc->length && doc->text[end] != '\n' && last) last--;
        doc->cursor = start + (col < last ? col : last);
    }
}

uint32_t writer_words(const writer_document_t *doc) {
    uint32_t count = 0;
    int in_word = 0;
    for (uint32_t i = 0; i < doc->length; i++) {
        int space = doc->text[i] == ' ' || doc->text[i] == '\n';
        if (!space && !in_word) count++;
        in_word = !space;
    }
    return count;
}

/* Minimal uncompressed, paginated PDF 1.4. All offsets and stream lengths
 * are byte-exact. Output is bounded before any filesystem write is attempted. */
#define PDF_PAGES 128u
#define PAGE_ROWS 50u
typedef struct { uint8_t *data; uint32_t length, capacity; int failed; } sink_t;
static void byte(sink_t *s, char c) {
    if (s->length >= s->capacity) { s->failed = 1; return; }
    if (s->data) s->data[s->length] = (uint8_t)c;
    s->length++;
}
static void string(sink_t *s, const char *text) { while (*text) byte(s, *text++); }
static void number(sink_t *s, uint32_t value, unsigned width) {
    char digits[12]; unsigned n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (width > n) { byte(s, '0'); width--; }
    while (n) byte(s, digits[--n]);
}
static void object(sink_t *s, uint32_t id, uint32_t *offsets) {
    offsets[id] = s->length; number(s, id, 0); string(s, " 0 obj\n");
}
static void emphasis(sink_t *s, unsigned old, unsigned wanted) {
    /* Keep bold outside italic so crossing style changes remain properly
     * nested Markdown, not crossing/unmatched emphasis delimiters. */
    if ((old ^ wanted) & WRITER_BOLD) {
        if (old & WRITER_ITALIC) byte(s, '*');
        if (old & WRITER_BOLD) string(s, "**");
        if (wanted & WRITER_BOLD) string(s, "**");
        if (wanted & WRITER_ITALIC) byte(s, '*');
    } else if ((old ^ wanted) & WRITER_ITALIC) byte(s, '*');
}
static void text_content(sink_t *s, const writer_document_t *doc, int markdown) {
    unsigned active = 0;
    uint32_t next_text = 0;
    for (uint32_t i = 0; i < doc->length; i++) {
        char c = doc->text[i];
        if (markdown) {
            unsigned wanted = doc->style[i] & 3;
            if (c == '\n') wanted = 0;
            else if (c == ' ') {
                if (next_text <= i) {
                    next_text = i;
                    while (next_text < doc->length && doc->text[next_text] == ' ') next_text++;
                }
                /* Delimiters must not surround leading/trailing whitespace. */
                unsigned next = next_text < doc->length &&
                    doc->text[next_text] != '\n' ? doc->style[next_text] : 0;
                wanted &= active & next;
                if ((active ^ next) & WRITER_BOLD) wanted &= ~WRITER_ITALIC;
            }
            emphasis(s, active, wanted); active = wanted;
            if (c == '*' || c == '\\') byte(s, '\\');
        } else if (c == '\n') byte(s, '\r');
        byte(s, c);
    }
    if (markdown) emphasis(s, active, 0);
}
int writer_export_text(const writer_document_t *doc, int markdown,
                       uint8_t *output, uint32_t capacity, uint32_t *size) {
    *size = 0;
    sink_t count = {0, 0, capacity, 0};
    text_content(&count, doc, markdown);
    if (count.failed) return -1;
    sink_t s = {output, 0, capacity, 0};
    text_content(&s, doc, markdown);
    *size = s.length;
    return 0;
}
/* Minimal Office Open XML package, using stored ZIP entries (no runtime
 * compressor). Preflight the whole package before touching the output. */
static void xml_text(sink_t *s, char c) {
    if (c == '&') string(s, "&amp;");
    else if (c == '<') string(s, "&lt;");
    else if (c == '>') string(s, "&gt;");
    else byte(s, c);
}
static void docx_part(sink_t *s, const writer_document_t *doc, unsigned part) {
    string(s, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
    if (part == 0) {
        string(s, "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
            "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
            "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
            "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
            "</Types>");
    } else if (part == 1) {
        string(s, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
            "</Relationships>");
    } else {
        string(s, "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body><w:p>");
        for (uint32_t i = 0; i < doc->length;) {
            if (doc->text[i] == '\n') {
                string(s, "</w:p><w:p>"); i++; continue;
            }
            unsigned style = doc->style[i] & 3;
            string(s, "<w:r>");
            if (style) {
                string(s, "<w:rPr>");
                if (style & WRITER_BOLD) string(s, "<w:b/>");
                if (style & WRITER_ITALIC) string(s, "<w:i/>");
                string(s, "</w:rPr>");
            }
            string(s, "<w:t xml:space=\"preserve\">");
            do { xml_text(s, doc->text[i++]); }
            while (i < doc->length && doc->text[i] != '\n' &&
                   (doc->style[i] & 3) == style);
            string(s, "</w:t></w:r>");
        }
        string(s, "</w:p><w:sectPr/></w:body></w:document>");
    }
}
static void zip16(sink_t *s, uint32_t n) {
    byte(s, (char)n); byte(s, (char)(n >> 8));
}
static void zip32(sink_t *s, uint32_t n) {
    zip16(s, n); zip16(s, n >> 16);
}
static uint32_t zip_crc(const uint8_t *data, uint32_t size) {
    uint32_t crc = 0xffffffffu;
    while (size--) {
        crc ^= *data++;
        for (unsigned i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
static void docx_package(sink_t *s, const writer_document_t *doc) {
    static const char *names[] = {"[Content_Types].xml", "_rels/.rels", "word/document.xml"};
    static const unsigned lengths[] = {19, 11, 17};
    uint32_t offsets[3], sizes[3], crcs[3];
    for (unsigned i = 0; i < 3; i++) {
        sink_t count = {0, 0, s->capacity, 0};
        docx_part(&count, doc, i);
        if (count.failed) { s->failed = 1; return; }
        sizes[i] = count.length; offsets[i] = s->length;
        zip32(s, 0x04034b50); zip16(s, 20);
        zip16(s, 0); zip16(s, 0); zip16(s, 0); zip16(s, 0x21);
        zip32(s, 0); zip32(s, sizes[i]); zip32(s, sizes[i]);
        zip16(s, lengths[i]); zip16(s, 0); string(s, names[i]);
        uint32_t start = s->length;
        docx_part(s, doc, i);
        crcs[i] = s->data && !s->failed ? zip_crc(s->data + start, sizes[i]) : 0;
        if (s->data && !s->failed) {
            for (unsigned j = 0; j < 4; j++)
                s->data[offsets[i] + 14 + j] = (uint8_t)(crcs[i] >> (j * 8));
        }
    }
    uint32_t central = s->length;
    for (unsigned i = 0; i < 3; i++) {
        zip32(s, 0x02014b50); zip16(s, 20); zip16(s, 20);
        zip16(s, 0); zip16(s, 0); zip16(s, 0); zip16(s, 0x21);
        zip32(s, crcs[i]); zip32(s, sizes[i]); zip32(s, sizes[i]);
        zip16(s, lengths[i]); zip16(s, 0); zip16(s, 0);
        zip16(s, 0); zip16(s, 0); zip32(s, 0); zip32(s, offsets[i]);
        string(s, names[i]);
    }
    uint32_t central_size = s->length - central;
    zip32(s, 0x06054b50); zip16(s, 0); zip16(s, 0);
    zip16(s, 3); zip16(s, 3); zip32(s, central_size);
    zip32(s, central); zip16(s, 0);
}
int writer_export_docx(const writer_document_t *doc, uint8_t *output,
                       uint32_t capacity, uint32_t *size) {
    *size = 0;
    sink_t count = {0, 0, capacity, 0};
    docx_package(&count, doc);
    if (count.failed) return -1;
    sink_t s = {output, 0, capacity, 0};
    docx_package(&s, doc);
    if (s.failed) return -1;
    *size = s.length;
    return 0;
}
static void page_content(sink_t *s, const writer_document_t *doc, uint32_t start) {
    string(s, "BT\n/F1 10 Tf\n13 TL\n48 744 Td\n");
    for (unsigned row = 0; row < PAGE_ROWS; row++) {
        uint32_t end = line_end(doc, start);
        for (uint32_t i = start; i < end;) {
            unsigned style = doc->style[i] & 3;
            string(s, "/F"); number(s, style + 1, 0); string(s, " 10 Tf\n(");
            do {
                char c = doc->text[i++];
                if (c == '(' || c == ')' || c == '\\') byte(s, '\\');
                byte(s, c);
            } while (i < end && (doc->style[i] & 3) == style);
            string(s, ") Tj\n");
        }
        string(s, "T*\n");
        if (end == doc->length) break;
        start = end + (doc->text[end] == '\n');
    }
    string(s, "ET\n");
}

int writer_export_pdf(const writer_document_t *doc, uint8_t *output,
                      uint32_t capacity, uint32_t *size) {
    uint32_t starts[PDF_PAGES], offsets[7 + PDF_PAGES * 2];
    uint32_t pages = 1, at = 0, row = 0;
    starts[0] = 0; *size = 0;
    for (;;) {
        uint32_t end = line_end(doc, at);
        if (end == doc->length) break;
        at = end + (doc->text[end] == '\n');
        if (++row == PAGE_ROWS) {
            if (pages == PDF_PAGES) return -1;
            starts[pages++] = at; row = 0;
        }
    }
    sink_t s = {output, 0, capacity, 0};
    string(&s, "%PDF-1.4\n");
    object(&s, 1, offsets); string(&s, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    object(&s, 2, offsets); string(&s, "<< /Type /Pages /Count ");
    number(&s, pages, 0); string(&s, " /Kids [");
    for (uint32_t p = 0; p < pages; p++) {
        number(&s, 7 + p * 2, 0); string(&s, " 0 R ");
    }
    string(&s, "] >>\nendobj\n");
    static const char *const fonts[] = {"Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique"};
    for (unsigned i = 0; i < 4; i++) {
        object(&s, 3 + i, offsets);
        string(&s, "<< /Type /Font /Subtype /Type1 /BaseFont /");
        string(&s, fonts[i]); string(&s, " >>\nendobj\n");
    }
    for (uint32_t p = 0; p < pages && !s.failed; p++) {
        object(&s, 7 + p * 2, offsets);
        string(&s, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792]\n"
                   "/Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >> /Contents ");
        number(&s, 8 + p * 2, 0); string(&s, " 0 R >>\nendobj\n");
        sink_t count = {0, 0, 0xFFFFFFFFu, 0};
        page_content(&count, doc, starts[p]);
        object(&s, 8 + p * 2, offsets);
        string(&s, "<< /Length "); number(&s, count.length, 0);
        string(&s, " >>\nstream\n"); page_content(&s, doc, starts[p]);
        string(&s, "endstream\nendobj\n");
    }
    if (s.failed) return -1;
    uint32_t xref = s.length, objects = 7 + pages * 2;
    string(&s, "xref\n0 "); number(&s, objects, 0);
    string(&s, "\n0000000000 65535 f \n");
    for (uint32_t id = 1; id < objects; id++) {
        number(&s, offsets[id], 10); string(&s, " 00000 n \n");
    }
    string(&s, "trailer\n<< /Size "); number(&s, objects, 0);
    string(&s, " /Root 1 0 R >>\nstartxref\n"); number(&s, xref, 0);
    string(&s, "\n%%EOF\n");
    if (s.failed) return -1;
    *size = s.length;
    return 0;
}
