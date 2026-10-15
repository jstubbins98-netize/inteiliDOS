#include "storage_dialog.h"
#include "../kernel/usb_storage.h"
#include "../kernel/keyboard.h"
#include "../kernel/memory.h"

int storage_choose_usb(storage_message_fn show) {
    const usb_volume_info_t *volumes=usb_storage_volumes();
    char label[80]="USB drive:";
    unsigned n=(unsigned)kstrlen(label), count=0;
    for (unsigned i=0;i<USB_STORAGE_MAX;i++) {
        if (!volumes[i].mounted) continue;
        const char item[]=" [0] USB0:";
        for (unsigned j=0;j<sizeof(item)-1;j++) label[n++]=item[j];
        label[n-8]=(char)('0'+i);
        label[n-2]=(char)('0'+i);
        count++;
    }
    kstrcpy(label+n," [Esc] Cancel");
    if (!count) {
        show("No USB drive mounted. Run USB /MOUNT in the shell. [any key]");
        keyboard_getchar();return -1;
    }
    show(label);
    for (;;) {
        int c=keyboard_getchar();
        if (c==KEY_ESCAPE) return -1;
        if (c>='0' && c<'0'+USB_STORAGE_MAX && volumes[c-'0'].mounted)
            return c-'0';
    }
}

int storage_choose_target(char *filename,unsigned capacity,int saving,
                          storage_message_fn show) {
    if (usb_path(filename)) return 3;
    show(saving ? "Save to: [1] C: HDD  [2] D: HDD  [3] USB drive  [Esc] Cancel" :
                  "Load from: [1] C: HDD  [2] D: HDD  [3] USB drive  [Esc] Cancel");
    for (;;) {
        int c=keyboard_getchar();
        if (c=='1' || c=='2') return c-'0';
        if (c==KEY_ESCAPE) return 0;
        if (c!='3') continue;
        int usb=storage_choose_usb(show);
        if (usb<0) return 0;
        unsigned len=(unsigned)kstrlen(filename);
        if (len+7>capacity) {
            show("USB filename is too long. [any key]");
            keyboard_getchar();return 0;
        }
        for (unsigned j=len+1;j>0;j--) filename[j+5]=filename[j-1];
        filename[0]='U';filename[1]='S';filename[2]='B';
        filename[3]=(char)('0'+usb);filename[4]=':';filename[5]='\\';
        return 3;
    }
}
