/* gcc -fsanitize=address,undefined tests/cdrom_command_test.c -o /tmp/cdrom-test */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../shell/cdrom_command.c"

static cdrom_drive_t table[4];
static char output[16384];
static int rescans,ejects,eject_drive,eject_error,reads,dir_error,launch_drive;
static int sector_error;
void vga_putchar(char c) {size_t n=strlen(output);assert(n+1<sizeof(output));output[n]=c;output[n+1]=0;}
void vga_puts(const char *s) {assert(strlen(output)+strlen(s)<sizeof(output));strcat(output,s);}
void vga_printf(const char *fmt,...) {
    va_list ap;va_start(ap,fmt);size_t n=strlen(output);
    vsnprintf(output+n,sizeof(output)-n,fmt,ap);va_end(ap);
}
const cdrom_drive_t *cdrom_drives(void) {return table;}
void cdrom_rescan_all(void) {rescans++;}
uint32_t cdrom_rescan_media(uint8_t i) {table[i].last_lba=0;return 0;}
int cdrom_eject(uint8_t i) {ejects++;eject_drive=i;return eject_error;}
int cdrom_read_sector(uint8_t i,uint32_t lba,uint8_t *buf) {
    (void)i;assert(lba==16);reads++;memset(buf,0,2048);
    if (sector_error) return -1;
    buf[0]=1;memcpy(buf+1,"CD001",5);buf[6]=1;
    memset(buf+40,' ',32);memcpy(buf+40,"TEST_DISC",9);return 0;
}
int iso9660_read_dir(uint8_t i,iso9660_dirent_t e[ISO9660_MAX_FILES]) {
    assert(i<CDROM_MAX_DRIVES);
    if (dir_error) return -1;
    memset(e,0,2*sizeof(*e));strcpy(e[0].name,"APPS");e[0].is_dir=1;
    strcpy(e[1].name,"HELLO.ELF");e[1].size=4096;return 2;
}
void launchpad_run_cdrom(uint8_t i) {launch_drive=i;}
static void reset(void) {
    memset(table,0,sizeof(table));output[0]=0;rescans=ejects=reads=0;
    eject_drive=launch_drive=-1;eject_error=dir_error=sector_error=0;
}
static int invoke(const char *s) {
    output[0]=0;const char *a[]={s};return cdrom_command(s ? 1:0,a);
}
static void drive(unsigned i,int disc) {
    table[i]=(cdrom_drive_t){1,i,disc ? 123:0,disc ? 2048:0};
}
int main(void) {
    reset();assert(invoke(NULL)==-1 && strstr(output,"No CD-ROM drive"));
    reset();assert(!invoke("/?") && !rescans);
    assert(invoke("/WRONG")==-1 && !rescans);
    const char *extra[]={"/LIST","extra"};assert(cdrom_command(2,extra)==-1);
    drive(0,0);assert(!invoke(NULL) && strstr(output,"No readable data disc"));
    assert(invoke("/RUN")==-1 && launch_drive==-1);
    assert(invoke("/LIST")==-1);
    drive(2,1);assert(!invoke(NULL) && strstr(output,"TEST_DISC"));
    assert(strstr(output,"Secondary master") && strstr(output,"248 KiB"));
    assert(!invoke("/list") && strstr(output,"<DIR> APPS"));
    assert(strstr(output,"HELLO.ELF (4096 bytes)"));
    assert(!invoke("/run") && launch_drive==2);
    sector_error=1;assert(!invoke(NULL) && strstr(output,"could not be read"));
    assert(!strstr(output,"TEST_DISC"));
    dir_error=1;assert(invoke("/LIST")==-1 && strstr(output,"Cannot list"));
    assert(!invoke("/eject") && eject_drive==2 && !table[2].last_lba);
    assert(!invoke("/EJECT") && eject_drive==0); /* first empty drive fallback */
    eject_error=1;assert(invoke("/EJECT")==-1 && strstr(output,"Eject failed"));
    assert(!strstr(output,"command accepted"));
    puts("PASS: info, real listings, CD-only run routing, eject, empty/missing media, I/O errors and option validation");
}
