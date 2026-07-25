#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdint.h>

/* Initialise the physical and virtual memory managers.
 * mb_info_phys: physical address of the Multiboot2 info structure.
 */
void   memory_init(uint32_t mb_info_phys);
void   heap_init(void);   /* call even without Multiboot so kmalloc works */
void  *kmalloc(size_t size);
void  *kcalloc(size_t nmemb, size_t size);
void   kfree(void *ptr);
void  *kmemset(void *dst, int c, size_t n);
void  *kmemcpy(void *dst, const void *src, size_t n);
int    kmemcmp(const void *a, const void *b, size_t n);
size_t memory_total_kb(void);
size_t memory_free_kb(void);

/* Simple string helpers used by the kernel before libc */
size_t kstrlen(const char *s);
int    kstrcmp(const char *a, const char *b);
int    kstrncmp(const char *a, const char *b, size_t n);
char  *kstrcpy(char *dst, const char *src);
char  *kstrncpy(char *dst, const char *src, size_t n);
char  *kstrcat(char *dst, const char *src);
char  *kstrtolower(char *s);
const char *kstrstr(const char *haystack, const char *needle);

#endif /* MEMORY_H */
