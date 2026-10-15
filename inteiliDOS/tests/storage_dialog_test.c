#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../shell/storage_dialog.h"
#include "../kernel/usb_storage.h"
#include "../kernel/keyboard.h"
static usb_volume_info_t volumes[4];
static const char *keys;
static unsigned pos;
static char last[80];
int keyboard_getchar(void) {assert(keys[pos]);return (unsigned char)keys[pos++];}
const usb_volume_info_t *usb_storage_volumes(void) {return volumes;}
int usb_path(const char *p) {return !strncmp(p,"USB",3) && strchr(p,':');}
size_t kstrlen(const char *s) {return strlen(s);}
char *kstrcpy(char *d,const char *s) {return strcpy(d,s);}
static void message(const char *s) {assert(strlen(s)<80);strcpy(last,s);}
static int choose(char *name,unsigned capacity,const char *input) {
    keys=input;pos=0;
    return storage_choose_target(name,capacity,1,message);
}
int main(void) {
    char fn[60]="NOTE.TXT";
    assert(choose(fn,sizeof(fn),"1")==1 && !strcmp(fn,"NOTE.TXT"));
    assert(choose(fn,sizeof(fn),"2")==2 && !strcmp(fn,"NOTE.TXT"));
    assert(!choose(fn,sizeof(fn),"\x1b") && !strcmp(fn,"NOTE.TXT"));
    assert(!choose(fn,sizeof(fn),"3x") && strstr(last,"No USB"));
    volumes[0].mounted=volumes[3].mounted=1;
    assert(choose(fn,sizeof(fn),"313")==3);
    assert(!strcmp(fn,"USB3:\\NOTE.TXT"));
    assert(strstr(last,"[0] USB0:") && strstr(last,"[3] USB3:"));
    assert(!strstr(last,"USB1:") && !strstr(last,"USB2:"));
    assert(choose(fn,sizeof(fn),"")==3); /* explicit path: no native prompt */
    strcpy(fn,"NOTE.TXT");
    assert(!choose(fn,sizeof(fn),"3\x1b") && !strcmp(fn,"NOTE.TXT"));
    assert(!choose(fn,10,"30x") && !strcmp(fn,"NOTE.TXT"));
    puts("PASS: native C/D selection, USB drive IDs and labels, unavailable-drive "
         "rejection, explicit paths, cancel and filename bounds");
}
