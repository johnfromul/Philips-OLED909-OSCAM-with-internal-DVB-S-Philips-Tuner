/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MTK_BRIDGE_PROTOCOL_H
#define MTK_BRIDGE_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
uint16_t mtk_be16(const uint8_t *p);
uint32_t mtk_be32(const uint8_t *p);
void mtk_put16(uint8_t *p, uint16_t v);
void mtk_put32(uint8_t *p, uint32_t v);
int mtk_capmt(const uint8_t *pmt, size_t length, uint16_t service,
              uint16_t pmt_pid, uint8_t *out, size_t capacity);
/* Zero means incomplete; -1 means an unsupported/invalid frame. */
int mtk_frame_size(const uint8_t *data, size_t available);
/* PAT result: PMT PID, 0 for service absent, -1 for invalid/other transport. */
int mtk_pat_pmt(const uint8_t *data, size_t length, uint16_t service, uint16_t tsid);
/* Validated CA_PMT: 1 has CA descriptor, 0 free-to-air, -1 malformed. */
int mtk_capmt_encrypted(const uint8_t *data, size_t length);
#endif
