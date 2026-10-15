#ifndef USB_COMMAND_H
#define USB_COMMAND_H
int usb_command(int argc,const char *argv[]);
int usb_command_list(const char *path);
int usb_command_type(const char *path);
int usb_command_copy(const char *src,const char *dst);
int usb_pick_and_copy(void);
#endif
