/* Emulator disk transport for the ASUS P525's Windows CE 5 block-driver ABI.
 * Replaces the TrueFFS stream entry points; the guest's partition manager and
 * IMGFS/FAT filesystem drivers still perform all filesystem operations. */
typedef unsigned int U32;
typedef unsigned short U16;
typedef unsigned char U8;
struct Buf { U8 *ptr; U32 size; };
struct Req { U32 sector, count, nbuf, status, callback; struct Buf buf[1]; };
// The kernel's debug printf moves between language builds, so the bridge stays silent.
static void debug(U32 a, U32 b, U32 c) { (void)a; (void)b; (void)c; }
U32 DSK_Init(const U16 *key) {
    U32 last=0;
    if (key) for(U32 i=0;i<256 && key[i];i++) last=key[i];
    U32 id=last=='1'?2:1;
    debug(0x1111,id,(U32)key);
    return id;
}
U32 DSK_Open(U32 id,U32 access,U32 share) { (void)access;(void)share;return id; }
U32 DSK_Close(U32 id) { (void)id;return 1; }
U32 DSK_Deinit(U32 id) { (void)id;return 1; }
U32 DSK_PowerDown(U32 id) { (void)id;return 1; }
U32 DSK_PowerUp(U32 id) { (void)id;return 1; }
U32 DSK_Read(U32 id,void *b,U32 n) { (void)id;(void)b;(void)n;return ~0u; }
U32 DSK_Write(U32 id,void *b,U32 n) { (void)id;(void)b;(void)n;return ~0u; }
U32 DSK_Seek(U32 id,U32 n,U32 kind) { (void)id;(void)n;(void)kind;return ~0u; }
U32 DllEntry(U32 a,U32 b,U32 c) { (void)a;(void)b;(void)c;return 1; }
static void copy(U8 *d,const U8 *s,U32 n) { while(n--) *d++=*s++; }
U32 DSK_IOControl(U32 id,U32 code,U8 *in,U32 ilen,U8 *out,U32 olen,U32 *ret) {
    debug(code,id,ilen);
    if (ret) *ret=0;
    /* root: 0x3FE0000 bytes, up to the 0x84000000 disk window; the disk's first 64 MiB sit there, the rest (V4.010 is 70 MiB) at 0x8C000000 */
    U32 sectors=id==2?130816:143360;
    U8 *base=(U8*)(id==2?0x80020000u:0x84000000u);
    code &= ~0xC000u; /* IOCTL access bits do not change the disk operation. */
    U32 legacy=code;
    if ((code&0xfffffc00u)==0x71c00u) legacy=(code-0x71c00u)/4+1;
    U32 op=legacy;
    if(legacy==1)op=3;else if(legacy==2)op=1;else if(legacy==3)op=2;
    else if(legacy==4)op=5;else if(legacy==5)op=6;else if(legacy==6)op=4;
    if(code==0x71800u)op=9;
    switch(op) {
    case 1:case 2: {
        if(!in || ilen<28)return 0;
        struct Req *q=(struct Req*)in;
        if(q->nbuf==0 || q->nbuf>64 || ilen<20+q->nbuf*8 || q->sector>=sectors || q->count>sectors-q->sector) {q->status=87;return 0;}
        U32 pos=q->sector*512, left=q->count*512;
        for(U32 i=0;i<q->nbuf;i++) {
            U32 n=q->buf[i].size;
            if(n>left || !q->buf[i].ptr){q->status=87;return 0;}
            for(U32 k=0;k<n;){ /* split at the 64 MiB window edge */
                U32 at=pos+k, m=n-k;
                U8 *d=base+at;
                if(id!=2){ if(at<0x4000000u){ if(m>0x4000000u-at)m=0x4000000u-at; } else d=(U8*)0x8C000000u+(at-0x4000000u); }
                if(op==1)copy(q->buf[i].ptr+k,d,m);else copy(d,q->buf[i].ptr+k,m);
                k+=m;
            }
            pos+=n;left-=n;
        }
        q->status=left?87:0;
        if(ret)*ret=q->count*512-left;
        return left==0;
    }
    case 3: {
        U32 *p=(U32*)(out?out:in),n=out?olen:ilen;
        if(!p || n<24)return 0;
        p[0]=sectors;p[1]=512;p[2]=sectors/32;p[3]=1;p[4]=32;p[5]=8;
        if(ret)*ret=24;
        return 1;
    }
    case 4:case 5:return 1;
    case 6: {
        static const U16 name[]={'F','l','a','s','h',' ','D','i','s','k',0};
        if(!out || olen<sizeof(name))return 0;
        copy(out,(const U8*)name,sizeof(name));if(ret)*ret=sizeof(name);return 1;
    }
    case 9: {
        U32 *p=(U32*)in;
        if(!p || ilen<80)return 0;
        U32 cb=p[0];for(U32 i=4;i<cb && i<ilen;i++)in[i]=0;
        static const U16 profile[]={'T','R','U','E','F','F','S','_','D','O','C','1',0};
        U16 *s=(U16*)(in+4);for(U32 i=0;i<(id==2?12:11);i++)s[i]=profile[i];
        p[17]=1;p[18]=2;p[19]=1;
        if(ret)*ret=80;return 1;
    }
    default:return 0;
    }
}
