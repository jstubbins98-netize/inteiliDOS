#ifndef STORAGE_DIALOG_H
#define STORAGE_DIALOG_H
/* Returns 1=C:, 2=D:, 3=USB (filename rewritten with explicit USBn:),
 * 0=cancel. Caller supplies its own terminal/status-row renderer. */
typedef void (*storage_message_fn)(const char *);
int storage_choose_usb(storage_message_fn show); /* 0..3, -1=cancel */
int storage_choose_target(char *filename,unsigned capacity,int saving,
                          storage_message_fn show);
#endif
