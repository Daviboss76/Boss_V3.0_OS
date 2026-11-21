#include "kernel.h"
#include "vga.h"
#include "keyboard.h"
#include "rtc.h"
#include "pci.h"
#include "blk.h"
#include "fs.h"
#include "fsck.h"
#include "drv.h"
#include "heal.h"
#include "timer.h"
#include "paging.h"
#include "sched.h"
#include "disktool.h"
#include "ui.h"
#include "terminal.h"
#include "ota.h"
#include "ota_net.h"
#include "net.h"

#define COR_TXT VGA_COR(VGA_CINZA, VGA_PRETO)
#define COR_PS1 VGA_COR(VGA_VERDE_CL, VGA_PRETO)

#define OTA_BUF_MAX 262144u /* 256 KB para imagens de kernel */

static char filebuf[32768];            /* limite de cat/write/cp/crc: 32 KB */
static uint8_t ota_buf[OTA_BUF_MAX];
static bool sair;
static bool ota_pronto;

/* TODO: conecte aqui a sua funcao real de envio (ex.: TCP/UDP/socket ou net_send_frame) */
static int boss_ota_tx(const void *data, uint32_t len) {
    (void)data;
    (void)len;
    return -1;
}

/* TODO: conecte aqui a sua funcao real de recepcao (retorna qtd lida, 0 = sem dados, <0 = erro) */
static int boss_ota_rx(void *buf, uint32_t max_len) {
    (void)buf;
    (void)max_len;
    return -1;
}

static char *prox(char **s) {
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
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}
static uint32_t hex32(const char *s) {
    uint32_t v = 0;
    if (!s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s++;
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = 10u + (uint32_t)(c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10u + (uint32_t)(c - 'A');
        else break;
        v = (v << 4) | d;
    }
    return v;
}
static void dois(uint32_t v) { vga_putc('0' + v / 10); vga_putc('0' + v % 10); }
static void erro(int e) {
    vga_puts("erro: ");
    vga_puts(e <= -20 ? proc_strerror(e) : fs_strerror(e));
    vga_putc('\n');
}
static void falta(void) { vga_puts("faltou argumento\n"); }
static void drv_erro(int e) {
    vga_puts("driver: ");
    vga_puts(e == DRV_ENOENT ? "inexistente" :
             e == DRV_EQUAR  ? "em quarentena (veja 'heal')" :
                               "falhou mesmo apos reiniciar (veja 'heal')");
    vga_putc('\n');
}

static void ota_erro(ota_status_t st) {
    vga_puts("ota: ");
    switch (st) {
    case OTA_OK:           vga_puts("ok\n"); break;
    case OTA_ERR_REDE:     vga_puts("falha de rede\n"); break;
    case OTA_ERR_TAMANHO:  vga_puts("tamanho invalido\n"); break;
    case OTA_ERR_CRC:      vga_puts("CRC32 nao confere\n"); break;
    case OTA_ERR_FS_WRITE: vga_puts("erro ao gravar no FAT32\n"); break;
    case OTA_ERR_FS_RM:    vga_puts("erro ao remover KERNEL.BIN antigo\n"); break;
    case OTA_ERR_FS_FLUSH: vga_puts("erro no flush do disco\n"); break;
    default:               vga_puts("erro desconhecido\n"); break;
    }
}

static void garantir_ota_init(void) {
    if (!ota_pronto) {
        ota_bind_fat32();
        ota_pronto = true;
    }
}

static int executar_ota_arquivo(const char *path, uint32_t crc_esp, bool reboot_depois) {
    garantir_ota_init();

    bool is_dir = false;
    uint32_t tam_arq = 0;
    int r = fs_stat(path, &is_dir, &tam_arq);
    if (r) { erro(r); return -1; }
    if (is_dir) { erro(FS_EISDIR); return -1; }
    if (tam_arq == 0 || tam_arq > OTA_BUF_MAX) {
        vga_puts("ota: arquivo vazio ou maior que o buffer (max ");
        put_dec(OTA_BUF_MAX);
        vga_puts(" bytes)\n");
        return -1;
    }

    uint32_t lido = 0;
    r = fs_read(path, ota_buf, OTA_BUF_MAX, &lido);
    if (r) { erro(r); return -1; }

    uint32_t crc_calc = ota_crc32(ota_buf, lido);
    vga_puts("ota: lidos "); put_dec(lido);
    vga_puts(" bytes, crc32=0x"); put_hex(crc_calc, 8); vga_putc('\n');

    ota_status_t st = ota_aplicar_imagem(ota_buf, lido, crc_esp, reboot_depois);
    if (st != OTA_OK) {
        ota_erro(st);
        return -1;
    }

    vga_puts("ota: KERNEL.BIN atualizado com seguranca no FAT32\n");
    return 0;
}

static int executar_ota_rede(const char *host, const char *path, bool reboot_depois) {
    garantir_ota_init();

    ota_http_cfg_t cfg = {
        .host_header   = (host && host[0]) ? host : "10.0.2.2:8080",
        .path          = (path && path[0]) ? path : "/KERNEL.BIN",
        .tx            = boss_ota_tx,
        .rx            = boss_ota_rx,
        .download_buf  = ota_buf,
        .buf_cap       = OTA_BUF_MAX,
        .max_poll_iter = 500000u
    };

    vga_puts("ota: baixando de ");
    vga_puts(cfg.host_header);
    vga_puts(cfg.path);
    vga_puts("...\n");

    ota_status_t st = ota_baixar_e_aplicar(&cfg, reboot_depois);
    if (st != OTA_OK) {
        ota_erro(st);
        return -1;
    }

    vga_puts("ota: download e atualizacao concluídos\n");
    return 0;
}

static void ler_linha(char *buf, int max) {
    int n = 0;
    for (;;) {
        int c = kb_getc();
        if (c == '\n') { vga_putc('\n'); buf[n] = 0; return; }
        if (c == '\b') { if (n > 0) { n--; vga_putc('\b'); } continue; }
        if (c >= 32 && c < 127 && n < max - 1) { buf[n++] = c; vga_putc(c); }
    }
}

/* ---------- basicos ---------- */
static void c_clear(char *a)  { (void)a; vga_clear(COR_TXT); }
static void c_ver(char *a)    { (void)a; vga_puts("BossOS 0.4 - kernel em C, 32 bits, paginacao, processos ELF, ring 3\n"); }
static void c_echo(char *a)   { vga_puts(a); vga_putc('\n'); }
static void c_exit(char *a)   { (void)a; sair = true; }
static void c_ui(char *a)     { (void)a; ui_run(); vga_clear(COR_TXT); }
static void c_reboot(char *a) { (void)a; outb(0x64, 0xFE); for (;;) __asm__ volatile ("hlt"); }
static void c_disktool(char *a) { (void)a; disktool_run(); }
static void c_install(char *a)  { (void)a; install_run(); }

static void c_uptime(char *a) {
    (void)a; uint32_t s = ticks / 100;
    put_dec(s / 3600); vga_putc(':'); dois((s / 60) % 60); vga_putc(':'); dois(s % 60);
    vga_putc('\n');
}
static void c_lspci(char *a) {
    (void)a;
    for (int i = 0; i < pci_count(); i++) {
        pci_dev_t *d = pci_get(i);
        put_hex(d->bus, 2); vga_putc(':'); put_hex(d->slot, 2); vga_putc('.'); put_dec(d->func);
        vga_putc(' '); put_hex(d->vendor, 4); vga_putc(':'); put_hex(d->device, 4);
        vga_putc(' '); vga_puts(pci_class_name(d->class_code));
        if (d->driver) { vga_puts("  ["); vga_puts(d->driver); vga_putc(']'); }
        vga_putc('\n');
    }
}
static void c_date(char *a) {
    (void)a; int32_t r;
    if (drv_call("rtc", 1, 0, 0, 0, 0, &r) == DRV_OK) {
        dois((r >> 16) & 0xFF); vga_putc(':'); dois((r >> 8) & 0xFF); vga_putc(':'); dois(r & 0xFF);
        vga_puts("  [ring 3]\n");
    } else {
        rtc_hora_t t; rtc_ler(&t);
        dois(t.h); vga_putc(':'); dois(t.m); vga_putc(':'); dois(t.s);
        vga_puts("  [fallback ring 0]\n");
    }
}
static void c_disks(char *a) {
    (void)a;
    vga_puts("backend: "); vga_puts(blk_backend()); vga_puts("   erros de E/S: "); put_dec(blk_erros());
    vga_puts("\ndiscos fisicos (somente leitura fora do disktool):\n");
    for (int i = 0; i < disk_count(); i++) {
        const blk_info_t *d = disk_info(i);
        vga_puts("  "); vga_puts(d->nome); vga_puts(": "); vga_puts(d->modelo);
        vga_puts("  "); put_dec(d->setores / 2048); vga_puts(" MB\n");
    }
    vga_puts("volumes BOSSOS (visiveis ao FAT32):\n");
    for (int i = 0; i < blk_count(); i++) {
        int dk; uint32_t ini, tam;
        blk_vol_info(i, &dk, &ini, &tam);
        vga_puts("  "); vga_puts(blk_info(i)->nome); vga_puts(": disco"); put_dec(dk);
        vga_puts(", inicio "); put_dec(ini / 2048); vga_puts(" MB, "); put_dec(tam / 2048); vga_puts(" MB\n");
    }
}
static void c_mem(char *a) {
    (void)a;
    vga_puts("frames: "); put_dec(frames_used() * 4); vga_puts(" KB usados de ");
    put_dec(frames_total() * 4); vga_puts(" KB\n");
    vga_puts("paginacao ligada; texto e rodata do kernel somente leitura (WP)\n");
}
static void c_smash(char *a) {
    (void)a;
    vga_puts("tentando escrever no texto do kernel...\n");
    *(volatile uint8_t *)(uintptr_t)terminal_run = 0;
    vga_puts("(nao deveria chegar aqui)\n");
}

/* ---------- arquivos ---------- */
static void c_df(char *a) {
    (void)a; uint32_t t, l;
    int r = fs_info(&t, &l); if (r) { erro(r); return; }
    vga_puts("total: "); put_dec(t); vga_puts(" KB   livre: "); put_dec(l);
    vga_puts(" KB   usado: "); put_dec(t - l); vga_puts(" KB\n");
}
static void c_pwd(char *a) { (void)a; vga_puts(fs_cwd()); vga_putc('\n'); }
static void c_cd(char *a) { char *p = prox(&a); int r = fs_chdir(p ? p : "/"); if (r) erro(r); }
static void ls_cb(const char *nome, bool dir, uint32_t tam) {
    vga_puts(nome);
    for (int l = (int)strlen(nome); l < 14; l++) vga_putc(' ');
    if (dir) vga_puts("<DIR>"); else put_dec(tam);
    vga_putc('\n');
}
static void c_ls(char *a) { char *p = prox(&a); int r = fs_list(p ? p : ".", ls_cb); if (r) erro(r); }
static void c_cat(char *a) {
    char *p = prox(&a); if (!p) { falta(); return; }
    uint32_t n; int r = fs_read(p, filebuf, sizeof filebuf, &n);
    if (r) { erro(r); return; }
    for (uint32_t i = 0; i < n; i++) if (filebuf[i] != '\r') vga_putc(filebuf[i]);
    if (n && filebuf[n - 1] != '\n') vga_putc('\n');
}
static void c_write(char *a) {
    char *p = prox(&a); if (!p) { falta(); return; }
    while (*a == ' ') a++;
    uint32_t n = strlen(a);
    if (n > sizeof filebuf - 2) n = sizeof filebuf - 2;
    memcpy(filebuf, a, n); filebuf[n++] = '\n';
    int r = fs_write(p, filebuf, n); if (r) erro(r);
}
static void c_touch(char *a) {
    char *p = prox(&a); if (!p) { falta(); return; }
    bool d; uint32_t t;
    int r = fs_stat(p, &d, &t);
    if (r == FS_ENOENT) r = fs_write(p, "", 0);
    if (r) erro(r);
}
static void c_mkdir(char *a) { char *p = prox(&a); if (!p) { falta(); return; } int r = fs_mkdir(p); if (r) erro(r); }
static void c_rm(char *a)    { char *p = prox(&a); if (!p) { falta(); return; } int r = fs_remove(p); if (r) erro(r); }
static void c_cp(char *a) {
    char *s = prox(&a), *d = prox(&a);
    if (!s || !d) { falta(); return; }
    uint32_t n; int r = fs_read(s, filebuf, sizeof filebuf, &n);
    if (!r) r = fs_write(d, filebuf, n);
    if (r) erro(r);
}
static void c_hexdump(char *a) {
    char *p = prox(&a); if (!p) { falta(); return; }
    uint32_t n; int r = fs_read(p, filebuf, 256, &n);
    if (r) { erro(r); return; }
    for (uint32_t o = 0; o < n; o += 16) {
        put_hex(o, 4); vga_puts(": ");
        for (uint32_t i = 0; i < 16; i++) {
            if (o + i < n) put_hex((uint8_t)filebuf[o + i], 2); else vga_puts("  ");
            vga_putc(' ');
        }
        for (uint32_t i = 0; i < 16 && o + i < n; i++) { char c = filebuf[o + i]; vga_putc(c >= 32 && c < 127 ? c : '.'); }
        vga_putc('\n');
    }
}
static void c_fsck(char *a) {
    char *p = prox(&a); bool rep = p && !strcmp(p, "-r");
    uint32_t ac, re;
    if (fsck_run(rep, &ac, &re)) { vga_puts("fsck: nenhum volume BOSSOS\n"); return; }
    vga_puts("problemas: "); put_dec(ac);
    if (rep) { vga_puts("  setores reparados: "); put_dec(re); fs_mount(); }
    else if (ac) vga_puts("  (use 'fsck -r' para reparar)");
    vga_putc('\n');
}

/* ---------- atualizacao (OTA) ---------- */
static void c_ota(char *a) {
    char *arq = prox(&a);
    if (!arq) { falta(); return; }
    char *crc_s = prox(&a);
    uint32_t crc_esp = crc_s ? hex32(crc_s) : 0;
    executar_ota_arquivo(arq, crc_esp, false);
}
static void c_ota_apply(char *a) {
    char *arq = prox(&a);
    if (!arq) { falta(); return; }
    char *crc_s = prox(&a);
    uint32_t crc_esp = crc_s ? hex32(crc_s) : 0;
    executar_ota_arquivo(arq, crc_esp, true);
}
static void c_ota_fetch(char *a) {
    char *host = prox(&a);
    char *path = prox(&a);
    executar_ota_rede(host, path, false);
}
static void c_ota_net_apply(char *a) {
    char *host = prox(&a);
    char *path = prox(&a);
    executar_ota_rede(host, path, true);
}

/* ---------- ring 3 / autorreparo ---------- */
static void c_drv(char *a) {
    (void)a;
    static const char *const est[] = { "descarregado", "ativo", "QUARENTENA" };
    for (int i = 0; i < drv_count(); i++) {
        drv_stat_t s; drv_stat(i, &s);
        vga_puts(s.nome);
        for (int l = (int)strlen(s.nome); l < 6; l++) vga_putc(' ');
        vga_puts(est[s.estado]);
        for (int l = (int)strlen(est[s.estado]); l < 14; l++) vga_putc(' ');
        vga_puts("chamadas="); put_dec(s.chamadas);
        vga_puts(" falhas="); put_dec(s.falhas);
        vga_puts(" reinicios="); put_dec(s.reinicios); vga_putc('\n');
    }
}
static void c_beep(char *a) {
    char *p = prox(&a); uint32_t hz = p ? num(p) : 880;
    p = prox(&a);       uint32_t ms = p ? num(p) : 200;
    int32_t r; int e = drv_call("beep", 1, hz, ms, 0, 0, &r);
    if (e) drv_erro(e); else if (r) vga_puts("frequencia invalida (20..20000)\n");
}
static void c_crc(char *a) {
    char *p = prox(&a); if (!p) { falta(); return; }
    uint32_t n; int r = fs_read(p, filebuf, sizeof filebuf, &n);
    if (r) { erro(r); return; }
    int32_t v; int e = drv_call("crc", 1, 0, 0, 0, 0, &v);
    for (uint32_t o = 0; !e && o < n; o += 256)
        e = drv_call("crc", 2, 0, 0, filebuf + o, n - o > 256 ? 256 : n - o, &v);
    if (!e) e = drv_call("crc", 3, 0, 0, 0, 0, &v);
    if (e) { drv_erro(e); return; }
    vga_puts("crc32 = "); put_hex((uint32_t)v, 8); vga_puts("  [ring 3]\n");
}
static void c_crash(char *a) {
    char *p = prox(&a); uint32_t op = p ? num(p) : 0;
    int32_t r = 0; int e = drv_call("bug", op, 0, 0, 0, 0, &r);
    if (e) drv_erro(e); else { vga_puts("ok, ret="); put_dec(r); vga_putc('\n'); }
}
static void c_oops(char *a) {
    (void)a;
    vga_puts("provocando #UD no kernel (ring 0)...\n");
    __asm__ volatile ("ud2");
    vga_puts("isto nao deveria aparecer\n");
}
static void c_heal(char *a) {
    char *p = prox(&a);
    if (p && !strcmp(p, "check")) {
        int r = heal_verify();
        vga_puts(r == 0 ? "texto do kernel integro\n" :
                 r > 0  ? "texto do kernel estava corrompido: RESTAURADO\n" :
                          "falha ao restaurar o texto do kernel\n");
    } else heal_print();
}
static void c_corrupt(char *a) {
    (void)a;
    heal_canario_danificar();
    vga_puts("1 byte do texto do kernel foi corrompido. Rode: heal check\n");
}

/* ---------- processos ---------- */
static void c_run(char *a) {
    char *p = prox(&a); if (!p) { falta(); return; }
    int pid = proc_spawn_file(p);
    if (pid < 0) { erro(pid); return; }
    int cod = 0;
    proc_wait(pid, &cod);
    vga_puts("[processo "); put_dec(pid); vga_puts(" terminou, codigo "); vga_puts(cod < 0 ? "-" : ""); put_dec(cod < 0 ? -cod : cod); vga_puts("]\n");
}
static void c_bg(char *a) {
    char *p = prox(&a); if (!p) { falta(); return; }
    int pid = proc_spawn_file(p);
    if (pid < 0) { erro(pid); return; }
    vga_puts("[processo "); put_dec(pid); vga_puts(" em segundo plano]\n");
}
static void c_ps(char *a) {
    (void)a;
    static const char *const est[] = { "livre", "pronto", "rodando", "dorme", "tecla", "filho", "zumbi" };
    vga_puts("pid nome            estado   cpu(ticks)  RAM(KB)\n");
    for (int i = 0; i < 8; i++) {
        char nome[16]; int e; uint32_t cpu, kb;
        if (proc_info(i, nome, &e, &cpu, &kb)) continue;
        put_dec(i); vga_puts("   "); vga_puts(nome);
        for (int l = (int)strlen(nome); l < 16; l++) vga_putc(' ');
        vga_puts(est[e]);
        for (int l = (int)strlen(est[e]); l < 9; l++) vga_putc(' ');
        put_dec(cpu); vga_puts("          "); put_dec(kb); vga_putc('\n');
    }
}
static void c_kill(char *a) {
    char *p = prox(&a); if (!p) { falta(); return; }
    proc_kill((int)num(p));
}

typedef struct { const char *nome, *ajuda; void (*fn)(char *); } cmd_t;
static void c_help(char *a);
static const cmd_t cmds[] = {
    { "help",          "lista os comandos",               c_help },
    { "clear",         "limpa a tela",                    c_clear },
    { "ver",           "versao do sistema",               c_ver },
    { "uptime",        "tempo ligado",                    c_uptime },
    { "echo",          "echo <texto>",                    c_echo },
    { "date",          "hora (driver rtc em ring 3)",     c_date },
    { "lspci",         "dispositivos PCI",                c_lspci },
    { "disks",         "discos fisicos e volumes",        c_disks },
    { "disktool",      "gerenciador de discos/particoes", c_disktool },
    { "install",       "instalador guiado",               c_install },
    { "df",            "espaco do volume BOSSOS",         c_df },
    { "pwd",           "diretorio atual",                 c_pwd },
    { "cd",            "cd <pasta>",                      c_cd },
    { "ls",            "ls [pasta]",                      c_ls },
    { "cat",           "cat <arquivo>",                   c_cat },
    { "write",         "write <arquivo> <texto>",         c_write },
    { "touch",         "touch <arquivo>",                 c_touch },
    { "mkdir",         "mkdir <pasta>",                   c_mkdir },
    { "rm",            "rm <arquivo|pasta vazia>",        c_rm },
    { "cp",            "cp <origem> <destino>",           c_cp },
    { "hexdump",       "hexdump <arquivo> (256 bytes)",   c_hexdump },
    { "run",           "run <prog.elf> (Ctrl+C mata)",    c_run },
    { "bg",            "bg <prog.elf> (segundo plano)",   c_bg },
    { "ps",            "lista processos",                 c_ps },
    { "kill",          "kill <pid>",                      c_kill },
    { "mem",           "memoria e paginacao",             c_mem },
    { "smash",         "escreve no texto do kernel (#PF)",c_smash },
    { "drv",           "drivers de ring 3 e estado",      c_drv },
    { "beep",          "beep [hz] [ms] (ring 3)",         c_beep },
    { "crc",           "crc <arquivo> (CRC32 em ring 3)", c_crc },
    { "crash",         "crash <0-6> testa o driver bug",  c_crash },
    { "oops",          "falha em ring 0 (recuperavel)",   c_oops },
    { "corrupt",       "corrompe 1 byte do kernel",       c_corrupt },
    { "heal",          "heal | heal check",               c_heal },
    { "fsck",          "fsck [-r] verifica/repara a FAT", c_fsck },
    { "ota",           "ota <arq> [crc_hex] atualiza",    c_ota },
    { "ota_apply",     "ota_apply <arq> [crc] + reboot",  c_ota_apply },
    { "ota_fetch",     "ota_fetch [host:porta] [/arq]",   c_ota_fetch },
    { "ota_net_apply", "ota_net_apply [host] [/arq]+reb", c_ota_net_apply },
    { "ui",            "abre a interface",                c_ui },
    { "reboot",        "reinicia",                        c_reboot },
    { "exit",          "volta ao menu",                   c_exit },
};
#define N_CMDS ((int)(sizeof(cmds) / sizeof(cmds[0])))

static void c_help(char *a) {
    (void)a;
    for (int i = 0; i < N_CMDS; i++) {
        vga_puts(cmds[i].nome);
        for (int l = (int)strlen(cmds[i].nome); l < 10; l++) vga_putc(' ');
        vga_puts(cmds[i].ajuda); vga_putc('\n');
        if ((i + 1) % 20 == 0 && i + 1 < N_CMDS) {
            vga_puts("-- tecla --"); kb_getc(); vga_puts("\r            \r");
        }
    }
}

static void executa(const char *cmd, char *args) {
    for (int i = 0; i < N_CMDS; i++)
        if (!strcmp(cmd, cmds[i].nome)) { cmds[i].fn(args); return; }
    vga_puts("comando desconhecido: "); vga_puts(cmd); vga_putc('\n');
}

void terminal_run(void) {
    char buf[96];
    kjmp_t chk;
    kjmp_t *prev = oops_ctx;
    sair = false;
    vga_clear(COR_TXT);
    vga_puts("Terminal BossOS. Digite 'help'.\n");
    while (!sair) {
        vga_setcolor(COR_PS1);
        vga_puts("boss:"); vga_puts(fs_cwd()); vga_puts("> ");
        vga_setcolor(COR_TXT);
        ler_linha(buf, sizeof buf);

        char *args = buf;
        char *cmd = prox(&args);
        if (!cmd) continue;
        while (*args == ' ') args++;

        if (ksetjmp(&chk) == 0) {
            oops_ctx = &chk;
            executa(cmd, args);
        } else {
            __asm__ volatile ("cld; sti" ::: "memory");
            vga_setcolor(COR_TXT);
            vga_puts("[heal] comando abortado, o terminal continua\n");
        }
    }
    oops_ctx = prev;
}

