/* A source-generated Windows icon: no image library or external assets. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void u16(FILE *file, uint16_t n) { fwrite(&n, 2, 1, file); }
static void u32(FILE *file, uint32_t n) { fwrite(&n, 4, 1, file); }
int main(void) {
    FILE *file;
    int x, y;
    unsigned char mask[128] = {0};
    if (fopen_s(&file, "assets/picoslip.ico", "wb")) return 1;
    u16(file,0); u16(file,1); u16(file,1);
    fputc(32,file); fputc(32,file); u16(file,0); u16(file,1); u16(file,32);
    u32(file,40+4096+128); u32(file,22);
    u32(file,40); u32(file,32); u32(file,64); u16(file,1); u16(file,32);
    u32(file,0); u32(file,4096); u32(file,0); u32(file,0); u32(file,0); u32(file,0);
    for (y=31; y>=0; --y) for (x=0; x<32; ++x) {
        uint32_t pixel = 0;
        if (x>=3 && x<29 && y>=3 && y<29) pixel=0xffffdf75;
        if (x>=22 && y>=22) pixel=(x+y<51 && x<29 && y<29) ? 0xffd5b34b : 0;
        if (x>=8 && x<24 && (y==10 || y==11 || y==16 || y==17)) pixel=0xff2c2b23;
        if (x>=8 && x<18 && (y==22 || y==23)) pixel=0xff2c2b23;
        u32(file,pixel);
    }
    fwrite(mask,1,sizeof(mask),file);
    return fclose(file) ? 1 : 0;
}
