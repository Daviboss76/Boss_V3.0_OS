#include "kernel.h"
#include "blk.h"
#include "fsck.h"

#define SEC 512
static uint8_t s0[SEC] __attribute__((aligned(4)));
static uint8_t bpb[SEC] __attribute__((aligned(4)));
static uint32_t fa[128], fb[128];

static int acha(int *dev, uint32_t *plba) {
    for (int d = 0; d < blk_count(); d++) {
        if (blk_read(d, 0, 1, s0) || s0[510] != 0x55 || s0[511] != 0xAA) continue;
        for (int p = 0; p < 4; p++) {
            uint8_t *e = s0 + 446 + p * 16;
            if (e[4] == 0x0B || e[4] == 0x0C) { memcpy(plba, e + 8, 4); *dev = d; return 0; }
        }
    }
    return -1;
}

int fsck_run(bool reparar, uint32_t *achados, uint32_t *reparados) {
    *achados = 0; *reparados = 0;
    int dev; uint32_t plba;
    if (acha(&dev, &plba)) return -1;
    if (blk_read(dev, plba, 1, bpb)) return -1;

    uint32_t rsv = bpb[14] | (bpb[15] << 8), nf = bpb[16], spc = bpb[13];
    uint32_t fsz, tot;
    memcpy(&fsz, bpb + 36, 4); memcpy(&tot, bpb + 32, 4);
    if (!tot) tot = bpb[19] | (bpb[20] << 8);
    if (!nf || !fsz || !spc) return -1;
    uint32_t clusters = (tot - (rsv + nf * fsz)) / spc;
    uint32_t fat1 = plba + rsv;

    for (uint32_t s = 0; s < fsz; s++) {
        int r1 = blk_read(dev, fat1 + s, 1, fa);
        int r2 = nf > 1 ? blk_read(dev, fat1 + fsz + s, 1, fb) : -1;
        if (r1 && r2) { (*achados)++; continue; }

        uint32_t *src = fa; bool regrava = false;
        if (r1) { src = fb; regrava = true; (*achados)++; }                       /* FAT1 ilegivel: usa FAT2 */
        else if (nf > 1 && (r2 || memcmp(fa, fb, SEC))) { regrava = true; (*achados)++; }  /* espelho diferente */

        for (uint32_t i = 0; i < 128; i++) {
            uint32_t idx = s * 128 + i;
            if (idx < 2) continue;
            uint32_t v = src[i] & 0x0FFFFFFF;
            bool ok = v == 0 || (v >= 2 && v < clusters + 2) || v >= 0x0FFFFFF7u;
            if (!ok) { src[i] = (src[i] & 0xF0000000u) | 0x0FFFFFFFu; regrava = true; (*achados)++; }
        }
        if (regrava && reparar) {
            bool ok = true;
            for (uint32_t k = 0; k < nf; k++)
                if (blk_write(dev, fat1 + k * fsz + s, 1, src)) ok = false;
            if (ok) (*reparados)++;
        }
    }
    return 0;
}
