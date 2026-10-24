/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "protocol.h"
#include <string.h>

uint16_t mtk_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t mtk_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
void mtk_put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
void mtk_put32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static int descriptors_ok(const uint8_t *p, size_t n) {
    size_t at = 0;
    while (at < n) {
        if (n - at < 2 || p[at + 1] > n - at - 2) return 0;
        at += 2 + p[at + 1];
    }
    return 1;
}
int mtk_capmt(const uint8_t *pmt, size_t length, uint16_t service,
              uint16_t pmt_pid, uint8_t *out, size_t capacity) {
    if (length < 16 || pmt[0] != 2 || mtk_be16(pmt + 3) != service ||
        !(pmt[5] & 1) || pmt[6] != 0 || pmt[7] != 0) return -1;
    size_t end = 3 + (mtk_be16(pmt + 1) & 0x0fff);
    if (end != length || end > 4096) return -1;
    end -= 4;  // MPEG CRC already checked by the demux section filter.
    size_t program = mtk_be16(pmt + 10) & 0x0fff;
    if (program > end - 12 || !descriptors_ok(pmt + 12, program)) return -1;
    uint8_t payload[8192];
    size_t n = 0;
    payload[n++] = 3;  // CA_PMT_LIST_ONLY
    payload[n++] = pmt[3]; payload[n++] = pmt[4]; payload[n++] = pmt[5];
    size_t program_length = 1 + 13 + program;
    mtk_put16(payload + n, (uint16_t)(0xf000 | program_length)); n += 2;
    payload[n++] = 1;  // CA_PMT_CMD_OK_DESCRAMBLING
    const uint8_t devices[] = {0x83,1,0, 0x84,2,(uint8_t)(pmt_pid >> 8),(uint8_t)pmt_pid,
                               0x86,1,0, 0x87,1,0};
    memcpy(payload + n, devices, sizeof(devices)); n += sizeof(devices);
    memcpy(payload + n, pmt + 12, program); n += program;
    size_t at = 12 + program;
    while (at < end) {
        if (end - at < 5) return -1;
        size_t es = mtk_be16(pmt + at + 3) & 0x0fff;
        if (es > end - at - 5 || !descriptors_ok(pmt + at + 5, es) ||
            n + 6 + es > sizeof(payload)) return -1;
        memcpy(payload + n, pmt + at, 3); n += 3;
        mtk_put16(payload + n, (uint16_t)(0xf000 | (1 + es))); n += 2;
        payload[n++] = 1;
        memcpy(payload + n, pmt + at + 5, es); n += es;
        at += 5 + es;
    }
    size_t header = n < 128 ? 4 : 6;
    if (n + header > capacity) return -1;
    out[0] = 0x9f; out[1] = 0x80; out[2] = 0x32;
    if (header == 4) out[3] = (uint8_t)n;
    else { out[3] = 0x82; mtk_put16(out + 4, (uint16_t)n); }
    memcpy(out + header, payload, n);
    return (int)(n + header);
}
int mtk_frame_size(const uint8_t *data, size_t available) {
    if (available < 4) return 0;
    size_t length;
    switch (mtk_be32(data)) {
    case 0xffff0002:  // SERVER_INFO
        if (available < 7) return 0;
        length = 7 + data[6]; break;
    case 0x403c6f2b: length = 65; break;  // DMX_SET_FILTER
    case 0x00006f2a: length = 9; break;   // DMX_STOP
    case 0x40086f87: length = 13; break;  // CA_SET_PID (wire protocol only)
    case 0x40106f86: length = 21; break;  // CA_SET_DESCR (wire protocol only)
    case 0x400c6f88: length = 17; break;  // CA_SET_DESCR_MODE
    default: return -1;
    }
    return available < length ? 0 : (int)length;
}

int mtk_pat_pmt(const uint8_t *p, size_t n, uint16_t sid, uint16_t tsid) {
    if(n<12 || p[0]!=0 || !(p[1]&0x80) || !(p[5]&1) ||
       3+(size_t)(mtk_be16(p+1)&0x0fff)!=n || (tsid && mtk_be16(p+3)!=tsid) ||
       (n-12)%4 || p[6]>p[7])return -1;
    for(size_t at=8;at<n-4;at+=4)if(mtk_be16(p+at)==sid) {
        int pid=mtk_be16(p+at+2)&0x1fff;
        return pid>0 && pid<8191?pid:-1;
    }
    return 0;
}
static int has_ca(const uint8_t *p,size_t n) {
    int found=0;
    for(size_t at=0;at<n;) {
        if(n-at<2 || p[at+1]>n-at-2)return -1;
        if(p[at]==9) {if(p[at+1]<4)return -1;found=1;}
        at+=2+p[at+1];
    }
    return found;
}
int mtk_capmt_encrypted(const uint8_t *p,size_t n) {
    if(n<4 || p[0]!=0x9f || p[1]!=0x80 || p[2]!=0x32)return -1;
    size_t head=p[3]==0x82?6:4;
    if(n<head+7 || (head==6?mtk_be16(p+4):p[3])!=n-head)return -1;
    size_t len=mtk_be16(p+head+4)&0x0fff;
    if(!len || len>n-head-6)return -1;
    int encrypted=has_ca(p+head+7,len-1);
    if(encrypted<0)return -1;
    for(size_t at=head+6+len;at<n;) {
        if(n-at<6)return -1;
        len=mtk_be16(p+at+3)&0x0fff;
        if(!len || len>n-at-5)return -1;
        int value=has_ca(p+at+6,len-1);
        if(value<0)return -1;
        encrypted|=value;at+=5+len;
    }
    return encrypted;
}
