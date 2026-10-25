#ifndef WRITER_DOCUMENT_H
#define WRITER_DOCUMENT_H
#include <stdint.h>
#define WRITER_CAPACITY 16384u
#define WRITER_WIDTH 76u
#define WRITER_BOLD 1u
#define WRITER_ITALIC 2u
typedef struct {
    char text[WRITER_CAPACITY];
    uint32_t length, cursor;
    int dirty;
    uint8_t style[WRITER_CAPACITY];
    uint8_t typing_style;
} writer_document_t;
void writer_document_init(writer_document_t *doc);
int writer_document_load(writer_document_t *doc, const uint8_t *data, uint32_t size);
int writer_document_load_markdown(writer_document_t *doc, const uint8_t *data, uint32_t size);
int writer_has_formatting(const writer_document_t *doc);
int writer_insert(writer_document_t *doc, char character);
void writer_backspace(writer_document_t *doc);
void writer_delete_line(writer_document_t *doc);
void writer_position(const writer_document_t *doc, uint32_t *row, uint32_t *col);
int writer_visual_line(const writer_document_t *doc, uint32_t row,
                       uint32_t *start, uint32_t *end);
void writer_vertical(writer_document_t *doc, int direction);
uint32_t writer_words(const writer_document_t *doc);
int writer_export_text(const writer_document_t *doc, int markdown,
                       uint8_t *output, uint32_t capacity, uint32_t *size);
int writer_export_pdf(const writer_document_t *doc, uint8_t *output,
                      uint32_t capacity, uint32_t *size);
int writer_export_docx(const writer_document_t *doc, uint8_t *output,
                       uint32_t capacity, uint32_t *size);
#endif
