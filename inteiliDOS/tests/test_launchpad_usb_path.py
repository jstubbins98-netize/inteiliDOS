#!/usr/bin/env python3
"""Exercise the actual bounded LaunchPad USB path builder with sanitizers."""
from pathlib import Path
import os
import subprocess
import tempfile

source=(Path(__file__).resolve().parents[1]/"shell/launchpad.c").read_text()
start=source.index("static int lp_usb_path(")
end=source.index("\n}",start)+2
with tempfile.TemporaryDirectory(prefix="lp-usb-path-") as folder:
    folder=Path(folder)
    code=folder/"path.c"
    code.write_text(r'''
#include <assert.h>
#include <string.h>
#define LP_SRC_USB0 8
static int lp_depth;
static struct {char name[32];} lp_dirstack[8];
static void lp_strcpy(char *d,const char *s,int n){
    int i=0;for(;s[i]&&i<n-1;i++)d[i]=s[i];d[i]=0;
}
''' + source[start:end] + r'''
static struct {unsigned char before[16];char path[128];unsigned char after[16];} guard;
static void check_canaries(void){
    for(int i=0;i<16;i++)assert(guard.before[i]==0xa5&&guard.after[i]==0xa5);
}
int main(void){
    memset(&guard,0xa5,sizeof(guard));
    assert(lp_usb_path(LP_SRC_USB0+2,"GAME.ELF",guard.path));
    assert(!strcmp(guard.path,"USB2:\\GAME.ELF"));check_canaries();
    char name[123];memset(name,'x',121);name[121]=0;
    assert(lp_usb_path(LP_SRC_USB0,name,guard.path)&&strlen(guard.path)==127);
    name[121]='x';name[122]=0;
    assert(!lp_usb_path(LP_SRC_USB0,name,guard.path)&&!guard.path[0]);
    check_canaries();
    lp_depth=8;
    for(int i=0;i<8;i++){memset(lp_dirstack[i].name,'a',12);lp_dirstack[i].name[12]=0;}
    assert(lp_usb_path(LP_SRC_USB0,"FILENAME.ELF",guard.path));check_canaries();
    for(int i=0;i<8;i++){memset(lp_dirstack[i].name,'b',31);lp_dirstack[i].name[31]=0;}
    assert(!lp_usb_path(LP_SRC_USB0,0,guard.path)&&!guard.path[0]);check_canaries();
    assert(!lp_usb_path(LP_SRC_USB0,"GAME.ELF",guard.path));check_canaries();
    return 0;
}
''')
    binary=folder/"path"
    subprocess.run(["gcc","-std=c11","-O2","-Wall","-Wextra","-Werror",
                    "-fsanitize=address,undefined",str(code),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
print("PASS: real LaunchPad USB path builder boundaries, nested paths, failure and canaries")
