/* An isolated DOS-readable D: backed by native ATAPI (IDE or shared AHCI).
 * FAT is synthesized, not written to the optical medium or private DOS HDD.
 * DOS handles paths, EXEC, COPY and file handles itself. This is deliberately
 * not MSCDEX: no CD audio, packet commands, or raw guest hardware access. */
#include "cdrom.h"
#include "../cdrom.h"
#include "../memory.h"

#define NODES 4096
#define CLUSTERS 60000u
#define SPC 64u
#define FAT_SECTORS 235u
#define PARTITION 63u
#define ROOT_SECTORS 32u
#define DATA_START (1u+2u*FAT_SECTORS+ROOT_SECTORS)
#define VOLUME_SECTORS (DATA_START+CLUSTERS*SPC)
#define DISK_SECTORS (PARTITION+VOLUME_SECTORS)
#define NONE 0xffff
typedef struct {
    uint32_t lba,size;
    uint16_t parent,child,next,children,first,count;
    uint8_t name[11],dir,depth;
} node_t;
static node_t *nodes;
static uint16_t *fat,*owner;
static unsigned used,drive,iso_sectors;
static uint32_t cached;
static uint8_t cache[2048],pvd[2048],label[11];
static const char *error;
static uint16_t le16(const uint8_t *p) {return p[0]|(uint16_t)p[1]<<8;}
static uint32_t le32(const uint8_t *p) {return le16(p)|(uint32_t)le16(p+2)<<16;}
static void put16(uint8_t *p,unsigned v) {p[0]=v;p[1]=v>>8;}
static void put32(uint8_t *p,uint32_t v) {put16(p,v);put16(p+2,v>>16);}
static int source(uint32_t lba) {
    if (lba>=iso_sectors) return -1;
    if (cached!=lba) {
        cached=0xffffffff;
        if (cdrom_read_sector(drive,lba,cache)) return -1;
        cached=lba;
    }
    return 0;
}
static int extent(uint32_t lba,uint32_t size) {
    return lba<iso_sectors && size/2048+(size%2048!=0)<=iso_sectors-lba;
}
static unsigned char upper(unsigned char c) {
    return c>='a' && c<='z'?c-32:c;
}
static unsigned char legal(unsigned char c) {
    c=upper(c);
    if ((c>='A' && c<='Z') || (c>='0' && c<='9') ||
        c=='_' || c=='-' || c=='$' || c=='~' || c=='!') return c;
    return '_';
}
static int duplicate(unsigned parent,const uint8_t *name) {
    for (unsigned n=nodes[parent].child;n!=NONE;n=nodes[n].next)
        if (!kmemcmp(nodes[n].name,name,11)) return 1;
    return 0;
}
static void filename(unsigned parent,const uint8_t *s,unsigned length,uint8_t *out) {
    unsigned n=0,dot=0;
    while (n<length && s[n]!=';') {if (s[n]=='.') dot=n;++n;}
    if (n && s[n-1]=='.') --n;
    if (dot>=n) dot=0;
    unsigned base=dot?dot:n,ext=dot?n-dot-1:0;
    kmemset(out,' ',11);
    for (unsigned i=0;i<base && i<8;i++) out[i]=legal(s[i]);
    for (unsigned i=0;i<ext && i<3;i++) out[8+i]=legal(s[dot+1+i]);
    if (!base) out[0]='_';
    if (base>8 || ext>3 || duplicate(parent,out)) {
        static const char hex[]="0123456789ABCDEF";
        for (unsigned alias=1;alias<NODES;alias++) {
            for (unsigned i=0;i<4;i++) out[i]=i<base?legal(s[i]):'_';
            out[4]='~';out[5]=hex[alias>>8];out[6]=hex[(alias>>4)&15];
            out[7]=hex[alias&15];
            if (!duplicate(parent,out)) break;
        }
    }
}
void msdos_cdrom_close(void) {
    kfree(nodes);kfree(fat);kfree(owner);
    nodes=0;fat=owner=0;used=0;cached=0xffffffff;
}
const char *msdos_cdrom_error(void) {return error?error:"no readable ISO 9660 disc";}
uint32_t msdos_cdrom_sectors(void) {return nodes?DISK_SECTORS:0;}
int msdos_cdrom_open(void) {
    msdos_cdrom_close();error="no readable ISO 9660 disc";
    const cdrom_drive_t *devices=cdrom_drives();
    for (drive=0;drive<CDROM_MAX_DRIVES;drive++) {
        if (devices[drive].present!=CDROM_PRESENT) continue;
        cdrom_rescan_media(drive);
        if (cdrom_read_sector(drive,16,pvd)) continue;
        if (pvd[0]==1 && !kmemcmp(pvd+1,"CD001",5) && pvd[6]==1) break;
    }
    if (drive==CDROM_MAX_DRIVES) return -1;
    error="invalid or unsupported ISO 9660 filesystem";
    iso_sectors=le32(pvd+80);
    if (le16(pvd+128)!=2048 || pvd[156]<34 || !(pvd[181]&2) ||
        !extent(le32(pvd+158),le32(pvd+166))) return -1;
    nodes=kcalloc(NODES,sizeof(*nodes));
    fat=kmalloc((CLUSTERS+2)*sizeof(*fat));
    owner=kmalloc((CLUSTERS+2)*sizeof(*owner));
    error="not enough memory for DOS CD-ROM directory";
    if (!nodes || !fat || !owner) goto failed;
    kmemset(label,' ',sizeof(label));
    for (unsigned i=0;i<11 && pvd[40+i]!=' ';i++) label[i]=legal(pvd[40+i]);
    nodes[0].lba=le32(pvd+158);nodes[0].size=le32(pvd+166);
    nodes[0].dir=1;nodes[0].child=nodes[0].next=NONE;used=1;
    /* Breadth-first enumeration, bounded even for hostile cycles/deep trees.
     * Copy a record before descending: all reads share a single sector cache. */
    error="unsupported ISO directory, multi-extent file, or directory limit (4096 entries)";
    for (unsigned parent=0;parent<used;parent++) {
        node_t *p=nodes+parent;
        if (!p->dir) continue;
        unsigned last=NONE;
        for (uint32_t at=0;at<p->size;) {
            unsigned in=at%2048;
            if (source(p->lba+at/2048)) goto failed;
            unsigned len=cache[in];
            if (!len) {at+=2048-in;continue;}
            if (len<34 || len>2048-in || len>p->size-at) goto failed;
            uint8_t record[255];kmemcpy(record,cache+in,len);at+=len;
            unsigned names=record[32];
            if (!names || names>len-33) goto failed;
            if (names==1 && record[33]<2) continue;
            if (used==NODES || p->depth==32 || record[1] ||
                record[26] || record[27] || (record[25]&0x80) ||
                le16(record+28)!=1) goto failed;
            node_t *n=nodes+used;
            n->lba=le32(record+2);n->size=le32(record+10);
            if (!extent(n->lba,n->size)) goto failed;
            n->dir=!!(record[25]&2);n->depth=p->depth+1;
            if (n->dir) for (unsigned i=0;i<used;i++)
                if (nodes[i].dir && nodes[i].lba==n->lba) goto failed;
            filename(parent,record+33,names,n->name);
            n->parent=parent;n->child=n->next=NONE;
            if (last==NONE) p->child=used;else nodes[last].next=used;
            last=used++;++p->children;
        }
    }
    /* All unused clusters are bad/reserved: DOS sees zero writable free space. */
    for (unsigned c=0;c<CLUSTERS+2;c++) {fat[c]=0xfff7;owner[c]=NONE;}
    fat[0]=0xfff8;fat[1]=0xffff;
    unsigned next=2;
    error="ISO exceeds DOS view limits (about 1.8 GiB or 511 root entries)";
    if (nodes[0].children>511) goto failed;
    for (unsigned i=1;i<used;i++) {
        node_t *n=nodes+i;
        uint32_t size=n->dir?(n->children+2u)*32:n->size;
        unsigned count=size/(SPC*512)+(size%(SPC*512)!=0);
        if (count>CLUSTERS+2-next) goto failed;
        n->first=count?next:0;n->count=count;
        for (unsigned c=0;c<count;c++,next++) {
            fat[next]=c+1<count?next+1:0xffff;owner[next]=i;
        }
    }
    error=0;cached=0xffffffff;return 0;
failed:
    msdos_cdrom_close();return -1;
}
static void entry(uint8_t *out,const node_t *n) {
    kmemcpy(out,n->name,11);out[11]=n->dir?0x11:0x21;
    put16(out+24,0x21); /* 1980-01-01; no fabricated current timestamps */
    put16(out+26,n->first);put32(out+28,n->dir?0:n->size);
}
static void directory(unsigned index,uint32_t offset,uint8_t *out) {
    const node_t *p=nodes+index;
    unsigned child=p->child,skip=offset/32;
    unsigned special=index?2:1;
    for (unsigned i=special;i<skip && child!=NONE;i++) child=nodes[child].next;
    for (unsigned slot=0;slot<16;slot++) {
        unsigned pos=skip+slot;uint8_t *e=out+slot*32;
        if (!index && !pos) {kmemcpy(e,label,11);e[11]=8;}
        else if (index && pos<2) {
            kmemset(e,' ',11);e[0]='.';if (pos) e[1]='.';
            e[11]=0x10;put16(e+26,pos?nodes[p->parent].first:p->first);
        } else if (child!=NONE) {entry(e,nodes+child);child=nodes[child].next;}
    }
}
int msdos_cdrom_read(uint32_t sector,uint8_t *out) {
    if (!nodes || sector>=DISK_SECTORS) return -1;
    kmemset(out,0,512);
    if (!sector) {
        out[450]=6;put32(out+454,PARTITION);put32(out+458,VOLUME_SECTORS);
        out[447]=1;out[448]=1; /* CHS 0/1/1 = LBA 63 */
        unsigned end=DISK_SECTORS-1,c=end/(255*63),h=end/63%255;
        out[451]=h;out[452]=((c>>2)&0xc0)|((end%63)+1);out[453]=c;
        out[510]=0x55;out[511]=0xaa;return 0;
    }
    if (sector<PARTITION) return 0;
    sector-=PARTITION;
    if (!sector) {
        out[0]=0xeb;out[1]=0x3c;out[2]=0x90;kmemcpy(out+3,"INTLCD  ",8);
        put16(out+11,512);out[13]=SPC;put16(out+14,1);out[16]=2;
        put16(out+17,ROOT_SECTORS*16);out[21]=0xf8;
        put16(out+22,FAT_SECTORS);put16(out+24,63);put16(out+26,255);
        put32(out+28,PARTITION);put32(out+32,VOLUME_SECTORS);
        out[36]=0x81;out[38]=0x29;put32(out+39,le32(pvd+80)^le32(pvd+158));
        kmemcpy(out+43,label,11);kmemcpy(out+54,"FAT16   ",8);
        out[510]=0x55;out[511]=0xaa;return 0;
    }
    if (sector<1+2*FAT_SECTORS) {
        unsigned start=(sector-1)%FAT_SECTORS*256;
        for (unsigned i=0;i<256;i++)
            put16(out+i*2,start+i<CLUSTERS+2?fat[start+i]:0xfff7);
        return 0;
    }
    if (sector<DATA_START) {
        directory(0,(sector-1-2*FAT_SECTORS)*512,out);return 0;
    }
    sector-=DATA_START;
    unsigned c=sector/SPC+2,index=owner[c];
    if (index==NONE) return 0;
    node_t *n=nodes+index;
    uint32_t offset=((c-n->first)*SPC+sector%SPC)*512;
    if (n->dir) {directory(index,offset,out);return 0;}
    if (offset>=n->size) return 0;
    if (source(n->lba+offset/2048)) return -1;
    unsigned count=n->size-offset;if (count>512) count=512;
    kmemcpy(out,cache+offset%2048,count);return 0;
}
