/* Explicit, one-file-at-a-time USB import. Never recurse or copy a whole disk. */
#include "usb_command.h"
#include "storage_dialog.h"
#include "../kernel/usb_storage.h"
#include "../kernel/memory.h"
#include "../kernel/keyboard.h"
#include "../kernel/vga.h"

static void message(const char *text) {vga_puts(text);vga_putchar('\n');}
static int line(char *out,unsigned capacity) {
    unsigned n=0;out[0]=0;
    for (;;) {
        int c=keyboard_getchar();
        if (c==KEY_ESCAPE) {vga_putchar('\n');return -1;}
        if (c=='\r'||c=='\n') {out[n]=0;vga_putchar('\n');return (int)n;}
        if (c==KEY_BACKSPACE && n) {n--;vga_putchar('\b');}
        else if (c>=32 && c<127 && n+1<capacity) {out[n++]=(char)c;vga_putchar(c);}
    }
}
static int append(char path[128],const char *name) {
    unsigned len=(unsigned)kstrlen(path), n=(unsigned)kstrlen(name);
    if (len+n+2>128) return -1;
    path[len]='\\';kstrcpy(path+len+1,name);return 0;
}
int usb_pick_and_copy(void) {
    message("Copy ONE selected USB file. Mounting never copies files.");
    int drive=storage_choose_usb(message);
    if (drive<0) {message("Copy cancelled.");return 0;}
    char source[128]="USB0:", basename[13];
    source[3]=(char)('0'+drive);
    unsigned page=0;
    for (;;) {
        usb_file_info_t files[64];
        int count=usb_file_list(source,files,64);
        if (count<0) {message(usb_storage_error(count));return -1;}
        vga_printf("\nUSB file chooser - %s (page %u)\n",source,page+1);
        unsigned start=page*16, end=start+16;
        if (end>(unsigned)count) end=(unsigned)count;
        for (unsigned i=start;i<end;i++)
            vga_printf(" %u. %s%s\n",i-start+1,
                       files[i].directory ? "<DIR> " : "",files[i].name);
        if (!count) message("(empty directory)");
        if (count==64) message("Listing limit: 64 entries; use COPY with an exact path for others.");
        message("File number + Enter; folders open (not copied). N/P=page, 0=parent.");
        vga_puts("Choose file [Esc=cancel]: ");
        char choice[5];int n=line(choice,sizeof(choice));
        if (n<0) {message("Copy cancelled; no files copied.");return 0;}
        if (n==1 && (choice[0]=='n'||choice[0]=='N')) {
            if ((page+1)*16<(unsigned)count) page++;
            continue;
        }
        if (n==1 && (choice[0]=='p'||choice[0]=='P')) {if(page) page--;continue;}
        if (n==1 && choice[0]=='0') {
            unsigned len=(unsigned)kstrlen(source);
            while (len>5 && source[len-1]!='\\') len--;
            if (len>5) source[len-1]=0;
            page=0;continue;
        }
        unsigned pick=0;
        for (int i=0;i<n;i++) {
            if (choice[i]<'0'||choice[i]>'9') {pick=0;break;}
            pick=pick*10+(unsigned)(choice[i]-'0');
        }
        if (!pick || pick>16 || start+pick>(unsigned)count) continue;
        const usb_file_info_t *file=&files[start+pick-1];
        if (append(source,file->name)) {message("USB path is too long.");return -1;}
        page=0;
        if (file->directory) continue;
        kstrcpy(basename,file->name);
        break;
    }
    vga_printf("Selected file: %s\n",source);
    vga_printf("Destination filename [Enter=%s, Esc=cancel]: ",basename);
    char destination[128];
    int n=line(destination,sizeof(destination));
    if (n<0) {message("Copy cancelled; no files copied.");return 0;}
    if (!n) kstrcpy(destination,basename);
    int target=storage_choose_target(destination,sizeof(destination),1,message);
    if (!target) {message("Copy cancelled; no files copied.");return 0;}
    if (target!=3) {
        unsigned len=(unsigned)kstrlen(destination);
        if (len+4>sizeof(destination)) {message("Destination is too long.");return -1;}
        for (unsigned j=len+1;j>0;j--) destination[j+2]=destination[j-1];
        destination[0]=target==1 ? 'C' : 'D';destination[1]=':';destination[2]='\\';
    }
    vga_printf("Copy ONLY %s -> %s?\n",source,destination);
    message("An existing destination file will be replaced. [Y] Copy  [any other] Cancel");
    int c=keyboard_getchar();
    if (c!='y' && c!='Y') {message("Copy cancelled; no files copied.");return 0;}
    return usb_command_copy(source,destination);
}
