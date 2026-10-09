#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef TEST_GENERIC
#undef TINY386_ENABLE_FAST_PLANAR_REFRESH
#endif
#include "../winspire/vga.c"

uint32_t get_uticks(void) { return 0; }
static unsigned redraws;
static int redraw_y,redraw_h;
static void redraw(void *opaque,int x,int y,int w,int h)
{
    (void)opaque;(void)x;assert(w==320);
    redraws++;redraw_y=y;redraw_h=h;
}
static void snapshot(FILE *output,VGAState *s)
{
    vga_refresh(s,redraw,NULL,0);
    assert(fwrite(s->fb_dev->fb_data,1,320*240*2,output)==320*240*2);
}
static void mode(VGAState *s,int height,unsigned line,unsigned start)
{
    memset(s->fb_dev->fb_data,0,320*240*2);
    s->cr[1]=79;s->cr[9]=0;s->cr[0x17]=3;
    s->cr[0x12]=(height-1)&255;
    s->cr[7]=((height-1)&256) ? 2 : 0;
    s->cr[0x13]=line/8;s->cr[0x0c]=start>>8;s->cr[0x0d]=start;
}
int main(int argc,char **argv)
{
    assert(argc==2);
    uint8_t *ram=calloc(256*1024,1),*fb=calloc(320*240,2);
    VGAState *s=vga_init((char *)ram,256*1024,fb,320,240);
    assert(s);
    s->palette_quiet_polls=0;
    s->ar_index=0x20;s->ar[0x12]=15;
    s->sr[VGA_SEQ_PLANE_WRITE]=15;s->gr[VGA_GFX_MISC]=5;
    s->sr[VGA_SEQ_MEMORY_MODE]=6;s->gr[VGA_GFX_MODE]=0;
    s->gr[VGA_GFX_BIT_MASK]=255;
    for(unsigned i=0;i<16;i++) s->ar[i]=i;
    for(unsigned i=0;i<768;i++) s->palette[i]=(i*17+3)&63;
    uint32_t random=12345;
    for(unsigned i=0;i<256*1024;i++) {
        random=random*1664525u+1013904223u;ram[i]=random>>24;
    }
    FILE *output=fopen(argv[1],"wb");assert(output);
    mode(s,480,320,0);
    snapshot(output,s);
    assert(redraws);
    redraws=0;snapshot(output,s);
#ifndef TEST_GENERIC
    assert(redraws==0);
#endif
    vga_mem_write(s,123*80+11,0xaa);
    redraws=0;snapshot(output,s);
#ifndef TEST_GENERIC
    assert(redraws==1 && redraw_y==61 && redraw_h==1);
#endif
    vga_mem_write16(s,125*80+12,0x3456);snapshot(output,s);
    vga_mem_write32(s,127*80+13,0x12345678);snapshot(output,s);
    uint8_t pixels[12]={1,2,3,4,5,6,7,8,9,10,11,12};
    assert(vga_mem_write_string(s,129*80+75,pixels,sizeof(pixels)));
    snapshot(output,s);
    for(unsigned write_mode=1;write_mode<=3;write_mode++) {
        s->gr[VGA_GFX_MODE]=write_mode;
        s->latch=0x12345678;s->gr[VGA_GFX_SR_VALUE]=5;
        vga_mem_write(s,131*80+14+write_mode,0x96);snapshot(output,s);
    }
    s->gr[VGA_GFX_MODE]=0;
    s->palette[3]^=31;snapshot(output,s);
    mode(s,480,352,32);snapshot(output,s);
    mode(s,400,320,0);snapshot(output,s);
    mode(s,350,320,0);snapshot(output,s);
    mode(s,480,320,0);snapshot(output,s);
    s->cr[9]=1;snapshot(output,s);
    s->cr[9]=0;snapshot(output,s);
    s->cr[0x17]=2;snapshot(output,s);
    s->cr[0x17]=3;snapshot(output,s);
    s->ar[0x12]=1;snapshot(output,s);
    s->ar[0x12]=15;snapshot(output,s);
    s->sr[1]=8;snapshot(output,s);
    s->sr[1]=0;snapshot(output,s);
    fclose(output);
    struct timespec before,after;
    redraws=0;clock_gettime(CLOCK_MONOTONIC,&before);
    for(int i=0;i<100;i++) vga_refresh(s,redraw,NULL,0);
    clock_gettime(CLOCK_MONOTONIC,&after);
    double seconds=after.tv_sec-before.tv_sec+(after.tv_nsec-before.tv_nsec)/1e9;
    printf("unchanged-planar-frames=100 redraws=%u seconds=%.6f\n",redraws,seconds);
#ifndef TEST_GENERIC
    assert(redraws==0);
#endif
    vga_delete(s);free(ram);free(fb);
    puts("PASS: planar rendering, dirty writes and register/palette changes");
}
