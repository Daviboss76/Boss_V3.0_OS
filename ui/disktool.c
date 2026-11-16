#include "kernel.h"
#include "vga.h"
#include "keyboard.h"
#include "blk.h"
#include "fs.h"
#include "paging.h"
#include "part.h"
#include "disktool.h"

static pdisk_t pd;
static int dsel = -1;
static const char *const tnome[] = { "nenhuma", "MBR", "GPT", "GPT invalida" };

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
static void pad(uint32_t v, int w) {
    char t[12]; int n = ksnprintf(t, sizeof t, "%u", v);
    for (int i = n; i < w; i++) vga_putc(' ');
    vga_puts(t);
}
static void padS(const char *s, int w) { vga_puts(s); for (int l = (int)strlen(s); l < w; l++) vga_putc(' '); }

static bool confirma(const char *palavra) {
    char b[32];
    vga_puts("Para confirmar digite "); vga_puts(palavra); vga_puts(": ");
    lerl(b, sizeof b);
    if (strcmp(b, palavra)) { vga_puts("cancelado.\n"); return false; }
    return true;
}

static void perr(int r) {
    switch (r) {
    case 0:  vga_puts("ok.\n"); break;
    case -1: vga_puts("erro de E/S (ou escrita bloqueada).\n"); break;
    case -2: vga_puts("sem entrada livre na tabela (MBR aceita 4 primarias).\n"); break;
    case -3: vga_puts("parametros invalidos ou fora do espaco livre.\n"); break;
    case -4: vga_puts("o sistema de arquivos da particao e maior que o novo tamanho (ou desconhecido).\n"
                      "Encolha-o antes no sistema original (Windows/Linux) e tente de novo.\n"); break;
    case -5: vga_puts("nao ha espaco livre logo apos a particao.\n"); break;
    default: vga_puts("erro: "); vga_puts(fs_strerror(r)); vga_putc('\n');
    }
}

static bool pronto(bool editar) {
    if (dsel < 0 || dsel >= disk_count()) { vga_puts("escolha um disco: d <n>\n"); return false; }
    if (part_scan(dsel, &pd)) { vga_puts("erro lendo o disco\n"); return false; }
    if (editar && (pd.tabela < 1 || pd.tabela > 2 || pd.truncada || pd.total == 0xFFFFFFFFu)) {
        vga_puts("este disco nao pode ser editado aqui (sem tabela valida, >2 TB ou particoes demais)\n");
        return false;
    }
    return true;
}

static bool backup_ok(void) {
    int r = part_backup(dsel);
    if (!r) { vga_puts("backup da tabela salvo em PTBK"); put_dec(dsel); vga_puts(".BIN (comando z restaura)\n"); return true; }
    vga_puts("AVISO: backup em arquivo falhou ("); vga_puts(fs_strerror(r)); vga_puts(")\n");
    return confirma("SEM-BACKUP");
}

static void c_discos(void) {
    if (!disk_count()) { vga_puts("nenhum disco encontrado\n"); return; }
    for (int i = 0; i < disk_count(); i++) {
        const blk_info_t *b = disk_info(i);
        vga_puts(i == dsel ? "* " : "  "); vga_puts(b->nome); vga_puts("  "); vga_puts(b->modelo);
        vga_puts("  "); put_dec(b->setores / 2048); vga_puts(" MB\n");
    }
}

static void c_tabela(void) {
    if (!pronto(false)) return;
    vga_puts("tabela: "); vga_puts(tnome[pd.tabela]); vga_putc('\n');
    vga_puts(" #  inicio(MB)  tam(MB)  tipo           fs\n");
    for (int i = 0; i < pd.n; i++) {
        part_t *p = &pd.p[i];
        pad(i, 2); pad(p->inicio / 2048, 11); pad(p->setores / 2048, 9); vga_puts("  ");
        padS(part_tipo(&pd, p), 15); vga_puts(p->fs);
        if (p->dono) vga_puts("  [BOSSOS]");
        if (p->grande) vga_puts("  [>2TB]");
        vga_putc('\n');
    }
    if (pd.truncada) vga_puts("(lista truncada: ha mais particoes; edicao desativada)\n");
}

static void c_livres(void) {
    if (!pronto(false)) return;
    uint32_t li[8], lt[8]; int n = part_livres(&pd, li, lt, 8);
    if (!n) { vga_puts("sem espaco livre (>= 1 MB) neste disco\n"); return; }
    for (int i = 0; i < n; i++) {
        vga_puts("livre "); put_dec(i); vga_puts(": inicio "); put_dec(li[i] / 2048);
        vga_puts(" MB, tamanho "); put_dec(lt[i] / 2048); vga_puts(" MB\n");
    }
}

static void c_novo(char *a) {
    if (!pronto(true)) return;
    uint32_t li[8], lt[8]; int ng = part_livres(&pd, li, lt, 8);
    if (!ng) { vga_puts("sem espaco livre. Reduza uma particao no sistema original primeiro.\n"); return; }
    char *t1 = tok(&a), *t2 = tok(&a);
    int g = 0;
    if (t2) g = (int)num(t2); else for (int i = 1; i < ng; i++) if (lt[i] > lt[g]) g = i;
    if (g >= ng) { vga_puts("livre inexistente\n"); return; }
    uint32_t tam = (t1 && num(t1)) ? num(t1) * 2048u : lt[g];
    if (tam < 131072u) { vga_puts("minimo: 64 MB\n"); return; }
    if (tam > lt[g]) { vga_puts("maior que o espaco livre escolhido\n"); return; }
    vga_puts("Criar particao de "); put_dec(tam / 2048); vga_puts(" MB no livre "); put_dec(g);
    vga_puts(" (inicio "); put_dec(li[g] / 2048); vga_puts(" MB) do "); vga_puts(disk_info(dsel)->nome); vga_puts(".\n");
    vga_puts("Nada existente sera alterado; so uma entrada nova na tabela.\n");
    if (!confirma("SIM")) return;
    if (!backup_ok()) return;
    perr(part_criar(&pd, li[g], tam));
}

static void c_apagar(char *a) {
    if (!pronto(true)) return;
    char *t = tok(&a);
    if (!t || (int)num(t) >= pd.n) { vga_puts("uso: x <numero da particao>\n"); return; }
    int i = (int)num(t);
    part_t *p = &pd.p[i];
    vga_puts("APAGAR a particao "); put_dec(i); vga_puts(" ("); put_dec(p->setores / 2048);
    vga_puts(" MB, "); vga_puts(part_tipo(&pd, p)); vga_puts(", fs "); vga_puts(p->fs); vga_puts(")\n");
    vga_puts("Os dados deixam de ser acessiveis pela tabela. Isto NAO se desfaz sem backup.\n");
    if (p->dono && !confirma("APAGAR-BOSSOS")) return;
    if (!confirma("APAGAR")) return;
    if (!backup_ok()) return;
    perr(part_apagar(&pd, i));
}

static void c_redim(char *a) {
    if (!pronto(true)) return;
    char *t1 = tok(&a), *t2 = tok(&a);
    if (!t1 || !t2 || (int)num(t1) >= pd.n) { vga_puts("uso: r <particao> <novo tamanho em MB>\n"); return; }
    int i = (int)num(t1);
    uint32_t novo = num(t2) * 2048u;
    part_t *p = &pd.p[i];
    vga_puts("Redimensionar a particao "); put_dec(i); vga_puts(" de "); put_dec(p->setores / 2048);
    vga_puts(" MB para "); put_dec(novo / 2048); vga_puts(" MB (so a TABELA; fs "); vga_puts(p->fs); vga_puts(").\n");
    vga_puts("Para encolher, o sistema de arquivos ja precisa ter sido encolhido pelo SO dono dele.\n");
    if (!confirma("REDIMENSIONAR")) return;
    if (!backup_ok()) return;
    perr(part_redim(&pd, i, novo));
}

static void c_backup(void) {
    if (!pronto(false)) return;
    perr(part_backup(dsel));
}
static void c_restaurar(void) {
    if (!pronto(false)) return;
    vga_puts("Restaura a tabela do arquivo PTBK"); put_dec(dsel); vga_puts(".BIN (somente se for do mesmo disco).\n");
    if (!confirma("RESTAURAR")) return;
    perr(part_restaurar(dsel));
}
static void c_tnova(void) {
    if (!pronto(false)) return;
    if (pd.tabela != 0) { vga_puts("o disco ja tem tabela; recuso sobrescrever\n"); return; }
    vga_puts("Cria uma tabela MBR vazia no setor 0 (somente em disco SEM tabela).\n");
    if (!confirma("CRIAR-MBR")) return;
    perr(part_tabela_mbr(dsel));
}

/* ---------- instalador ---------- */
static char fnome[16][13];
static uint32_t ftam[16];
static int nf_;
static void cb_lista(const char *n, bool dir, uint32_t t) {
    if (!dir && nf_ < 16) { strcpy(fnome[nf_], n); ftam[nf_++] = t; }
}

void install_run(void) {
    char b[48];
    vga_clear(VGA_COR(VGA_CINZA, VGA_PRETO));
    vga_puts("== Instalador BossOS ==\n"
             "Cria UMA particao nova em espaco LIVRE, formata (FAT32, rotulo BOSSOS)\n"
             "e copia os arquivos do volume atual. Nao toca nas outras particoes,\n"
             "nao grava codigo de boot e nao altera o setor 0 de discos GPT.\n\n");
    if (!disk_count()) { vga_puts("nenhum disco encontrado\n"); return; }
    c_discos();
    vga_puts("\ndisco de destino (numero, ENTER cancela): ");
    lerl(b, sizeof b);
    if (!b[0]) return;
    dsel = (int)num(b);
    if (!pronto(true)) return;
    c_tabela();
    uint32_t li[8], lt[8]; int ng = part_livres(&pd, li, lt, 8);
    if (!ng) {
        vga_puts("\nNao ha espaco livre. No sistema original, encolha uma particao\n"
                 "(Windows: Gerenciamento de Disco > Reduzir Volume; Linux: gparted),\n"
                 "e volte aqui. Nao reduzo particoes de outros sistemas sozinho.\n");
        return;
    }
    c_livres();
    int g = 0;
    for (int i = 1; i < ng; i++) if (lt[i] > lt[g]) g = i;
    vga_puts("\ntamanho em MB (ENTER = todo o maior livre; minimo 64): ");
    lerl(b, sizeof b);
    uint32_t tam = b[0] ? num(b) * 2048u : lt[g];
    if (tam < 131072u || tam > lt[g]) { vga_puts("tamanho invalido\n"); return; }
    uint32_t ini = li[g];

    vga_puts("\nResumo: particao de "); put_dec(tam / 2048); vga_puts(" MB em "); vga_puts(disk_info(dsel)->nome);
    vga_puts(", inicio "); put_dec(ini / 2048); vga_puts(" MB.\n");
    if (!confirma("INSTALAR")) return;
    if (!backup_ok()) return;

    int od = -1; uint32_t oi = 0, ot = 0;
    bool tinha = blk_vol_info(0, &od, &oi, &ot);
    nf_ = 0;
    uint8_t *buf = 0; uint32_t pag = 0, off[16], o = 0;
    if (tinha) {                                   /* le a raiz do volume atual para a RAM */
        fs_chdir("/");
        fs_list(".", cb_lista);
        for (int i = 0; i < nf_; i++) pag += (ftam[i] + 4095) / 4096;
        if (pag) buf = (uint8_t *)frame_alloc_contig(pag);
        for (int i = 0; i < nf_ && buf; i++) {
            uint32_t l; off[i] = o;
            if (fs_read(fnome[i], buf + o, ftam[i], &l)) ftam[i] = 0;
            o += ((ftam[i] + 4095) / 4096) * 4096;
        }
    }
    int r = part_criar(&pd, ini, tam);
    vga_puts("criar particao: "); perr(r);
    if (!r) { r = part_mkfs(dsel, ini, tam); vga_puts("formatar FAT32: "); perr(r); }
    if (!r) {
        blk_rescan(); blk_prefer(dsel, ini);
        if (fs_mount() == 0) {
            fs_chdir("/");
            for (int i = 0; i < nf_ && buf; i++) {
                if (!ftam[i]) continue;
                int w = fs_write(fnome[i], buf + off[i], ftam[i]);
                vga_puts("copiar "); vga_puts(fnome[i]); vga_puts(": "); perr(w);
            }
            uint32_t t, l;
            if (fs_info(&t, &l) == 0) { vga_puts("volume novo: "); put_dec(t / 1024); vga_puts(" MB, "); put_dec(l / 1024); vga_puts(" MB livres\n"); }
        } else vga_puts("nao consegui montar o volume novo\n");
        if (tinha) { blk_rescan(); blk_prefer(od, oi); fs_mount(); }
    }
    for (uint32_t i = 0; i < pag; i++) frame_free((uint32_t)buf + i * 4096);
    vga_puts("\nPronto. A particao BOSSOS aparece em 'disks' como volume e e montada por qualquer\n"
             "BossOS iniciado de outro meio. Boot direto a partir dela vem na proxima etapa.\n");
}

void disktool_run(void) {
    char b[64];
    vga_puts("Gerenciador de discos BossOS. 'h' = ajuda.\n");
    if (dsel < 0 && disk_count() == 1) dsel = 0;
    for (;;) {
        vga_puts("disk> ");
        lerl(b, sizeof b);
        char *a = b, *c = tok(&a);
        if (!c) continue;
        if (!strcmp(c, "q")) return;
        else if (!strcmp(c, "h"))
            vga_puts("l            lista discos        d <n>   escolhe disco\n"
                     "p            particoes           f       espaco livre\n"
                     "n [MB] [g]   cria particao       x <p>   apaga particao\n"
                     "r <p> <MB>   redimensiona        b / z   backup / restaura tabela\n"
                     "t            cria MBR (disco sem tabela)   i  instalador   q  sai\n");
        else if (!strcmp(c, "l")) c_discos();
        else if (!strcmp(c, "d")) {
            char *t = tok(&a);
            if (!t || (int)num(t) >= disk_count()) vga_puts("disco invalido\n"); else dsel = (int)num(t);
        }
        else if (!strcmp(c, "p")) c_tabela();
        else if (!strcmp(c, "f")) c_livres();
        else if (!strcmp(c, "n")) c_novo(a);
        else if (!strcmp(c, "x")) c_apagar(a);
        else if (!strcmp(c, "r")) c_redim(a);
        else if (!strcmp(c, "b")) c_backup();
        else if (!strcmp(c, "z")) c_restaurar();
        else if (!strcmp(c, "t")) c_tnova();
        else if (!strcmp(c, "i")) install_run();
        else vga_puts("comando desconhecido (h = ajuda)\n");
    }
}
