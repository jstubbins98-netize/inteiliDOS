#ifndef USB_STORAGE_H
#define USB_STORAGE_H
#include <stdint.h>
#define USB_STORAGE_MAX 4
#define USB_FILE_MAX (64u * 1024u)
/* Explicit USB0:..USB3: names do not alias the native C:/D: volumes. */
typedef struct {
    int detected, mounted;
    uint32_t sectors;
    const char *status;
} usb_volume_info_t;
typedef struct { char name[13]; uint32_t size; int directory; } usb_file_info_t;
int usb_storage_mount_all(void);
int usb_storage_eject_all(void);
const usb_volume_info_t *usb_storage_volumes(void);
int usb_path(const char *path); /* case-insensitive USB prefix, including invalid IDs */
int usb_file_list(const char *path, usb_file_info_t *out, unsigned max);
int usb_file_read(const char *path, uint8_t *buf, uint32_t max, uint32_t *length);
/* Bounded random reads for disk images larger than the native heap.
 * Opens/closes each call, returns the full file size, and requires exact I/O. */
int usb_file_read_at(const char *path, uint32_t offset, uint8_t *buf,
                     uint32_t length, uint32_t *file_size);
int usb_file_write(const char *path, const uint8_t *buf, uint32_t length);
const char *usb_storage_error(int code);
#endif
