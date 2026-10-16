#ifndef USB_BOT_H
#define USB_BOT_H
#include <stdint.h>
typedef int (*usb_bot_bulk_t)(void *device, uint8_t *data, unsigned bytes, int write);
/* Caller serializes DMA, quarantines failed transports, and owns this scratch. */
int usb_bot_command(usb_bot_bulk_t bulk, void *device, uint32_t tag,
                    uint8_t scratch[512], uint8_t saved[512],
                    const uint8_t *cdb, unsigned cdb_len,
                    uint8_t *buffer, unsigned bytes, int write);
#endif
