/* Share the real USB transport fault model; all I/O is to disposable files. */
#define main legacy_storage_main
#include "usb_storage_test.c"
#undef main
int main(int argc,char **argv) {
    assert(argc==3);
    int verify=!strcmp(argv[1],"verify");
    media[0]=fopen(argv[2],"r+b");assert(media[0]);
    assert(!fseek(media[0],0,SEEK_END));
    blocks[0]=(unsigned)(ftell(media[0])/512);present[0]=1;
    assert(usb_storage_mount_all()==1);
    assert(strstr(usb_storage_volumes()[0].status,"exFAT"));
    uint8_t data[10023],out[10023];uint32_t length,size;
    for(unsigned i=0;i<sizeof(data);i++)data[i]=(uint8_t)(i*37+11);
    if(!verify){
        const uint8_t hello[]="REAL-USB-EXFAT-DATA\n";
        assert(!usb_file_write("USB0:\\HELLO.TXT",hello,sizeof(hello)-1));
        assert(!usb_file_write("USB0:\\LARGE.BIN",data,sizeof(data)));
        assert(!f_mkdir("0:\\Long folder"));
        assert(!usb_file_write("USB0:\\Long folder\\Long filename.txt",data,sizeof(data)));
        assert(!usb_file_write("USB0:\\CAF\x82.TXT",data,37));
    }
    assert(!usb_file_read("USB0:\\long FOLDER\\long FILENAME.txt",out,sizeof(out),&length));
    assert(length==sizeof(data)&&!memcmp(data,out,length));
    assert(!usb_file_read("USB0:\\caf\x90.txt",out,sizeof(out),&length)&&length==37);
    assert(!usb_file_read_at("USB0:\\LARGE.BIN",509,out,4500,&size));
    assert(size==sizeof(data)&&!memcmp(data+509,out,4500));
    usb_file_info_t entries[16];
    int n=usb_file_list("USB0:\\Long folder",entries,16);
    assert(n==1&&!strcmp(entries[0].name,"Long filename.txt")&&!entries[0].size_overflow);
    unsigned before=writes[0];
    assert(disk_read(0,out,(LBA_t)1<<32,1)==RES_PARERR);
    assert(disk_write(0,out,(LBA_t)1<<32,1)==RES_PARERR);
    assert(writes[0]==before);
    sync_fail[0]=1;
    assert(usb_storage_eject_all()<0&&present[0]);
    assert(strstr(usb_storage_volumes()[0].status,"DO NOT remove"));
    sync_fail[0]=0;
    assert(!usb_storage_eject_all()&&!present[0]);
    assert(!fclose(media[0]));
    puts("PASS: exFAT read/write, CP437 case folding, LFN, bounded streaming, safe eject");
    return 0;
}
