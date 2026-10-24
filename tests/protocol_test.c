/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/protocol.h"
int main(void) {
    uint8_t pmt[]={2,0xb0,18,0x33,0x92,0xc1,0,0,0xfb,0x6d,0xf0,0,
                   0x1b,0xfb,0x6d,0xf0,0,0,0,0,0};
    uint8_t out[256];
    int n=mtk_capmt(pmt,sizeof(pmt),13202,7020,out,sizeof(out));
    assert(n>0 && out[0]==0x9f && out[2]==0x32);
    assert(mtk_capmt_encrypted(out,(size_t)n)==0);
    assert(mtk_capmt_encrypted(out,(size_t)n-1)<0);
    uint8_t encrypted[]={2,0xb0,24,0x33,0x92,0xc1,0,0,0xfb,0x6d,0xf0,6,
        9,4,6,0x24,0xfb,0x76,0x1b,0xfb,0x6d,0xf0,0,0,0,0,0};
    int enc_size=mtk_capmt(encrypted,sizeof(encrypted),13202,7020,out,sizeof(out));
    assert(enc_size>0 && mtk_capmt_encrypted(out,(size_t)enc_size)==1);
    assert(mtk_capmt(pmt,sizeof(pmt)-1,13202,7020,out,sizeof(out))<0);
    assert(mtk_capmt(pmt,sizeof(pmt),13203,7020,out,sizeof(out))<0);
    assert(mtk_capmt(pmt,sizeof(pmt),13202,7020,out,8)<0);
    uint8_t bad[sizeof(pmt)];memcpy(bad,pmt,sizeof(pmt));bad[11]=10;
    assert(mtk_capmt(bad,sizeof(bad),13202,7020,out,sizeof(out))<0);
    memcpy(bad,pmt,sizeof(pmt));bad[16]=250;
    assert(mtk_capmt(bad,sizeof(bad),13202,7020,out,sizeof(out))<0);
    uint8_t frame[65]={0x40,0x3c,0x6f,0x2b};
    for(size_t i=0;i<sizeof(frame);i++)assert(mtk_frame_size(frame,i)==0);
    assert(mtk_frame_size(frame,sizeof(frame))==65);
    mtk_put32(frame,0x40106f86);
    assert(mtk_frame_size(frame,20)==0 && mtk_frame_size(frame,21)==21);
    mtk_put32(frame,0xffff0002);frame[6]=255;
    assert(mtk_frame_size(frame,65)==0);
    mtk_put32(frame,0x12345678);assert(mtk_frame_size(frame,4)<0);
    uint8_t pat[]={0,0xb0,17,0x0c,0x9d,0xc1,0,0,
        0x33,0x92,0xfb,0x6c,0x33,0x93,0xfb,0x76,0,0,0,0};
    assert(mtk_pat_pmt(pat,sizeof(pat),13202,3229)==7020);
    assert(mtk_pat_pmt(pat,sizeof(pat),13203,3229)==7030);
    assert(mtk_pat_pmt(pat,sizeof(pat),13204,3229)==0);
    assert(mtk_pat_pmt(pat,sizeof(pat),13202,3230)<0);
    assert(mtk_pat_pmt(pat,sizeof(pat)-1,13202,3229)<0);
    pat[5]&=0xfe;assert(mtk_pat_pmt(pat,sizeof(pat),13202,3229)<0);
    puts("protocol validation tests passed");return 0;
}
