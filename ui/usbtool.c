#include "kernel.h"
#include "vga.h"
#include "keyboard.h"
#include "blk.h"
#include "fs.h"
#include "paging.h"
#include "part.h"
#include "usb.h"
#include "usbtool.h"

extern const uint8_t blob_mbr[], blob_mbr_end[], blob_stage2[], blob_stage2_end[];

static void lerl(char *b, int max) {
    int n = 0;
    for (;;) {
        int c = kb_getc();
        if (c == '\n') { vga_putc('\n'); b[n] = 0; return; }
        if (c == '\b') { if (n > 0) { n--; vga_putc('\b'); } continue; }
        if (c >= 32 && c < 127 && n < max - 1) { b[n++] = c; vga_putc(c); }
    }
}
static char *tok(char **s) {
    char *p = *s;
    while (*p == ' ') p++;
    if (!*p) { *s = p; return 0; }
    char *ini = p;
    while (*p && *p != ' ') p++;
    if (*p) *p++ = 0;
    *s = p; return ini;
}
static uint32_t num(const char *s) {
    uint32_t v = 0;
    if (!s) return 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}
static bool confirma(const char *palavra) {
    char b[32];
    vga_puts("Para confirmar digite "); vga_puts(palavra); vga_puts(": ");
    lerl(b, sizeof b);
    if (strcmp(b, palavra)) { vga_puts("cancelado.\n"); return false; }
    return true;
}

static const char *const tipos[] = { "outro", "hub", "armazenamento", "teclado", "mouse", "audio" };

static void listar(void) {
    vga_puts(usb_log());
    vga_puts("\n#  end vel   vid:pid     tipo           nome\n");
    for (int i = 0; i < usb_ndev(); i++) {
        const usb_dev_t *d = usb_dev(i);
        put_dec(i); vga_puts("  "); put_dec(d->addr); vga_puts("   ");
        vga_puts(d->vel == 2 ? "HS " : d->vel == 1 ? "LS " : "FS ");
        vga_puts("  "); put_hex(d->vid, 4); vga_putc(':'); put_hex(d->pid, 4); vga_puts("  ");
        vga_puts(tipos[d->tipo]);
        for (int l = (int)strlen(tipos[d->tipo]); l < 15; l++) vga_putc(' ');
        vga_puts(d->nome); vga_putc('\n');
    }
}

static void discos(void) {
    for (int i = 0; i < disk_count(); i++) {
        const blk_info_t *b = disk_info(i);
        vga_puts(b->nome); vga_puts(disk_is_usb(i) ? " [USB] " : "       ");
        vga_puts(b->modelo); vga_puts("  "); put_dec(b->setores / 2048); vga_puts(" MB\n");
    }
}

static void reescaneia(void) {
    blk_init();
    fs_mount();
    listar();
}

static void audio(void) {
    char b[160];
    if (!usb_audio_count()) { vga_puts("nenhuma caixa de som USB (classe audio)\n"); return; }
    for (int i = 0; i < usb_audio_count(); i++) {
        usb_audio_info(i, b, sizeof b);
        vga_puts("audio "); put_dec(i); vga_puts(": "); vga_puts(b); vga_putc('\n');
        int v = usb_audio_volume(i, -1);
        if (v >= 0) { vga_puts("   volume atual: "); put_dec(v); vga_puts("%\n"); }
    }
}

/* ---------- grava o BossOS num pendrive (APAGA o pendrive) ---------- */
static char fnome[16][13];
static uint32_t ftam[16];
static int nf_;
static void cb_lista(const char *n, bool dir, uint32_t t) {
    if (!dir && nf_ < 16) { strcpy(fnome[nf_], n); ftam[nf_++] = t; }
}

static void gravar(char *a) {
    int d = -1;
    char *t = tok(&a);
    if (t) d = (int)num(t);
    if (d < 0 || !disk_is_usb(d)) {
        vga_puts("uso: g <numero do disco USB>   (veja 'd'; so aceita discos [USB])\n");
        return;
    }
    const blk_info_t *bi = disk_info(d);
    uint32_t total = bi->setores;
    if (total < 262144u) { vga_puts("pendrive pequeno demais (minimo 128 MB)\n"); return; }
    uint32_t tam = total - 2048u;
    if (tam > 0x7FFFF800u) tam = 0x7FFFF800u;

    int od = -1; uint32_t oi = 0, ot = 0;
    if (!blk_vol_info(0, &od, &oi, &ot)) { vga_puts("sem volume BOSSOS montado para copiar os arquivos do sistema\n"); return; }
    if (od == d) { vga_puts("este pendrive e o disco de onde o sistema esta rodando; use outro\n"); return; }

    vga_puts("Vai APAGAR TUDO em "); vga_puts(bi->nome); vga_puts(": "); vga_puts(bi->modelo);
    vga_puts(", "); put_dec(total / 2048); vga_puts(" MB.\n");
    vga_puts("Grava MBR + estagio 2, cria a particao FAT32 BOSSOS e copia o sistema.\n");
    if (!confirma("GRAVAR-USB")) return;

    fs_chdir("/");
    nf_ = 0;
    fs_list(".", cb_lista);
    uint32_t pag = 0, off[16], o = 0;
    for (int i = 0; i < nf_; i++) pag += (ftam[i] + 4095) / 4096;
    uint8_t *buf = pag ? (uint8_t *)frame_alloc_contig(pag) : 0;
    if (!buf) { vga_puts("sem memoria para ler os arquivos\n"); return; }
    for (int i = 0; i < nf_; i++) {
        uint32_t l; off[i] = o;
        if (fs_read(fnome[i], buf + o, ftam[i], &l)) ftam[i] = 0;
        o += ((ftam[i] + 4095) / 4096) * 4096;
    }

    static uint8_t s0[512] __attribute__((aligned(4)));
    uint32_t nst = (uint32_t)(blob_stage2_end - blob_stage2) / 512;
    int r = -1;
    if ((uint32_t)(blob_mbr_end - blob_mbr) == 512 && nst > 0 && nst < 2047) {
        memset(s0, 0, 512);
        memcpy(s0, blob_mbr, 446);
        uint32_t ini = 2048;
        s0[446] = 0x80; s0[447] = 0xFE; s0[448] = 0xFF; s0[449] = 0xFF; s0[450] = 0x0C;
        s0[451] = 0xFE; s0[452] = 0xFF; s0[453] = 0xFF;
        memcpy(s0 + 454, &ini, 4); memcpy(s0 + 458, &tam, 4);
        s0[510] = 0x55; s0[511] = 0xAA;
        disk_arm(d, 0, 2048);
        r = disk_write(d, 0, 1, s0);
        if (!r) r = disk_write(d, 1, nst, blob_stage2);
        disk_disarm();
    }
    vga_puts("MBR e estagio 2: "); vga_puts(r ? "ERRO\n" : "ok\n");
    if (!r) { r = part_mkfs(d, 2048, tam); vga_puts("formatar FAT32: "); vga_puts(r ? "ERRO\n" : "ok\n"); }
    if (!r) {
        blk_rescan(); blk_prefer(d, 2048);
        if (fs_mount() == 0) {
            fs_chdir("/");
            for (int i = 0; i < nf_; i++) {
                if (!ftam[i]) continue;
                int w = fs_write(fnome[i], buf + off[i], ftam[i]);
                vga_puts("copiar "); vga_puts(fnome[i]); vga_puts(w ? ": ERRO\n" : ": ok\n");
            }
        } else vga_puts("nao consegui montar o volume novo\n");
        blk_rescan(); blk_prefer(od, oi); fs_mount();
    }
    for (uint32_t i = 0; i < pag; i++) frame_free((uint32_t)buf + i * 4096);
    if (!r) vga_puts("\nPronto. Inicie o PC por este pendrive (menu de boot / USB-HDD).\n");
}

void usbtool_run(void) {
    char b[64];
    vga_puts("USB e pendrive. 'h' = ajuda.\n");
    for (;;) {
        vga_puts("usb> ");
        lerl(b, sizeof b);
        char *a = b, *c = tok(&a);
        if (!c) continue;
        if (!strcmp(c, "q")) return;
        else if (!strcmp(c, "h"))
            vga_puts("l            dispositivos USB e diagnostico\n"
                     "s            reenumera o barramento (apos plugar algo)\n"
                     "d            discos (inclui pendrives)\n"
                     "g <disco>    grava o BossOS no pendrive (APAGA o pendrive)\n"
                     "a            caixas de som USB\n"
                     "v <n> <0-100>  volume da caixa n      m <n> <0|1>  mudo\n"
                     "q            sai\n");
        else if (!strcmp(c, "l")) listar();
        else if (!strcmp(c, "s")) reescaneia();
        else if (!strcmp(c, "d")) discos();
        else if (!strcmp(c, "g")) gravar(a);
        else if (!strcmp(c, "a")) audio();
        else if (!strcmp(c, "v")) {
            char *n = tok(&a), *p = tok(&a);
            if (!n || !p) { vga_puts("uso: v <n> <0-100>\n"); continue; }
            int r = usb_audio_volume((int)num(n), (int)num(p));
            vga_puts(r ? "falhou (sem controle de volume?)\n" : "ok\n");
        }
        else if (!strcmp(c, "m")) {
            char *n = tok(&a), *p = tok(&a);
            if (!n || !p) { vga_puts("uso: m <n> <0|1>\n"); continue; }
            int r = usb_audio_mudo((int)num(n), num(p) != 0);
            vga_puts(r ? "falhou (sem controle de mudo?)\n" : "ok\n");
        }
        else vga_puts("comando desconhecido (h = ajuda)\n");
    }
}
