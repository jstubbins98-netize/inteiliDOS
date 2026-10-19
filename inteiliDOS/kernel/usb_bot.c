#include "usb_bot.h"
#include "memory.h"
static void le32(uint8_t *p,uint32_t v) {
    for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(i*8));
}
static uint32_t get32(const uint8_t *p) {
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
int usb_bot_command(usb_bot_bulk_t bulk,void *device,uint32_t tag,
                    uint8_t scratch[512],uint8_t saved[512],
                    const uint8_t *cdb,unsigned cdb_len,
                    uint8_t *buffer,unsigned bytes,int write) {
    if (!cdb || !cdb_len || cdb_len>16 || bytes>512 || (bytes && !buffer)) return -1;
    kmemset(scratch,0,31);
    le32(scratch,0x43425355);le32(scratch+4,tag);le32(scratch+8,bytes);
    scratch[12]=write ? 0 : 0x80;scratch[14]=cdb_len;
    kmemcpy(scratch+15,cdb,cdb_len);
    if (bulk(device,scratch,31,1)) return -1;
    if (bytes) {
        if (write) kmemcpy(scratch,buffer,bytes);
        if (bulk(device,scratch,bytes,write)) return -1;
        if (!write) kmemcpy(saved,scratch,bytes);
    }
    if (bulk(device,scratch,13,0) || get32(scratch)!=0x53425355 ||
        get32(scratch+4)!=tag || get32(scratch+8)>bytes || scratch[12]>1) return -1;
    /* CHECK CONDITION may legitimately leave the data phase unprocessed
     * (e.g. UNIT ATTENTION after hotplug). Keep BOT synchronized so the
     * caller can REQUEST SENSE. Successful commands still require exact I/O. */
    if (scratch[12]) return -2;
    if (get32(scratch+8)) return -1;
    if (bytes && !write) kmemcpy(buffer,saved,bytes);
    return 0;
}
