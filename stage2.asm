; Montar com: nasm -f bin stage2.asm -o stage2.bin

[bits 16]
[org 0x1000]

STAGE2_SECTORS   equ 50         ; mesmo valor do MBR
KERNEL_TMP_SEG   equ 0x1000     ; buffer temporario: 0x10000
KERNEL_TMP_ADDR  equ 0x10000
KERNEL_LOAD_ADDR equ 0x100000   ; destino final (1 MB)
SEC_BUF          equ 0x8000     ; buffer de 1 setor (BPB / diretorio)
FAT_BUF          equ 0x8200     ; buffer de 1 setor da FAT
VBE_INFO_BUF     equ 0x7000     ; buffer para VBE Controller Info
VBE_MODE_BUF     equ 0x7200     ; buffer para VBE Mode Info
VBE_DATA_ADDR    equ 0x500      ; local para guardar dados VBE para o kernel (0x500)

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x9000
    sti
    cld

    mov [BOOT_DRIVE], dl        ; drive vindo do MBR
    mov [part_ptr], si          ; tabela de particoes vinda do MBR

    mov si, msg_stage2
    call print_string

    call find_partition
    call load_bpb
    call find_kernel
    call load_kernel
    call init_vbe_detect_only   ; Apenas deteta e guarda dados VBE, mantendo o modo texto!
    call enable_a20
    call enter_protected_mode

hang:
    jmp hang

; =========================
; Achar particao FAT32 (tipo 0x0B ou 0x0C)
; =========================
find_partition:
    mov si, [part_ptr]
    mov cx, 4
.next:
    mov al, [si + 4]
    cmp al, 0x0B
    je .found
    cmp al, 0x0C
    je .found
    add si, 16
    loop .next
    mov si, part_msg
    jmp fatal
.found:
    mov eax, [si + 8]           ; LBA inicial da particao
    mov [PART_LBA], eax
    ret

; =========================
; Ler BPB e calcular geometria FAT32
; =========================
load_bpb:
    mov eax, [PART_LBA]
    mov cx, 1
    mov bx, SEC_BUF
    call read_sectors

    cmp word [SEC_BUF + 0x0B], 512      ; bytes por setor
    jne bad_fs
    cmp word [SEC_BUF + 0x16], 0        ; FAT16 size deve ser 0 em FAT32
    jne bad_fs

    mov al, [SEC_BUF + 0x0D]            ; setores por cluster
    mov [SPC], al

    movzx eax, word [SEC_BUF + 0x0E]    ; setores reservados
    add eax, [PART_LBA]
    mov [FAT_LBA], eax

    movzx ebx, byte [SEC_BUF + 0x10]    ; numero de FATs
    mov ecx, [SEC_BUF + 0x24]           ; setores por FAT
    imul ebx, ecx
    add eax, ebx
    mov [DATA_LBA], eax                 ; inicio da area de dados

    mov eax, [SEC_BUF + 0x2C]           ; cluster do diretorio raiz
    mov [ROOT_CLUSTER], eax
    ret

bad_fs:
    mov si, fs_msg
    jmp fatal

; =========================
; Cluster -> LBA
; =========================
cluster_to_lba:
    sub eax, 2
    movzx ecx, byte [SPC]
    mul ecx
    add eax, [DATA_LBA]
    ret

; =========================
; Proximo cluster na FAT (EAX -> EAX)
; =========================
next_cluster:
    push ebx
    push ecx
    push es
    mov ecx, eax
    shr eax, 7                  ; 128 entradas por setor
    add eax, [FAT_LBA]
    push ecx
    xor bx, bx
    mov es, bx                  ; FAT sempre em ES=0
    mov cx, 1
    mov bx, FAT_BUF
    call read_sectors
    pop ecx
    pop es
    and cx, 127
    shl cx, 2
    mov bx, cx
    mov eax, [bx + FAT_BUF]
    and eax, 0x0FFFFFFF
    pop ecx
    pop ebx
    ret

; =========================
; Procurar KERNEL.BIN no diretorio raiz
; =========================
find_kernel:
    mov eax, [ROOT_CLUSTER]
.cluster_loop:
    mov [CUR_CLUSTER], eax
    call cluster_to_lba
    movzx ecx, byte [SPC]
.sector_loop:
    push eax
    push ecx
    mov cx, 1
    mov bx, SEC_BUF
    call read_sectors

    mov si, SEC_BUF
    mov dx, 16                  ; 16 entradas de 32 bytes por setor
.entry_loop:
    cmp byte [si], 0
    je .not_found               ; fim do diretorio
    cmp byte [si], 0xE5
    je .skip                    ; entrada apagada
    test byte [si + 11], 0x18   ; ignora LFN, label e pastas
    jnz .skip
    mov di, kernel_name
    mov cx, 11
    push si
    repe cmpsb
    pop si
    je .found
.skip:
    add si, 32
    dec dx
    jnz .entry_loop

    pop ecx
    pop eax
    inc eax
    dec ecx
    jnz .sector_loop

    mov eax, [CUR_CLUSTER]
    call next_cluster
    cmp eax, 0x0FFFFFF8
    jb .cluster_loop

.not_found:
    mov si, knf_msg
    jmp fatal

.found:
    add sp, 8                   ; descarta EAX/ECX empilhados
    movzx eax, word [si + 0x14] ; cluster (parte alta)
    shl eax, 16
    mov ax, [si + 0x1A]         ; cluster (parte baixa)
    mov [KERNEL_CLUSTER], eax
    mov eax, [si + 0x1C]        ; tamanho em bytes
    mov [KERNEL_SIZE], eax
    ret

; =========================
; Carregar kernel em 0x10000 (modo real)
; =========================
load_kernel:
    mov eax, [KERNEL_CLUSTER]
    mov bx, KERNEL_TMP_SEG
    mov es, bx
.cluster_loop:
    push eax
    call cluster_to_lba
    movzx ecx, byte [SPC]
    xor bx, bx
    call read_sectors           ; le o cluster inteiro em ES:0

    movzx dx, byte [SPC]
    shl dx, 5                   ; setores * 0x20 paragrafos
    mov bx, es
    add bx, dx
    mov es, bx
    cmp bx, 0x9000
    jae too_big
    pop eax
    call next_cluster
    cmp eax, 0x0FFFFFF8
    jb .cluster_loop

    xor ax, ax
    mov es, ax
    ret

too_big:
    mov si, big_msg
    jmp fatal

; =========================
; Deteao VBE (Guarda info em 0x500 SEM ligar o modo grafico)
; =========================
init_vbe_detec_only:
init_vbe_detect_only:
    pushad

    ; Limpar area de dados VBE em 0x500 primeiro (coloca zeros)
    mov edi, VBE_DATA_ADDR
    xor ecx, ecx
    mov ecx, 8                  ; 32 bytes limpos
    xor eax, eax
    rep stosd

    ; 1. Obter informacoes do controlador VBE
    mov ax, 0x4F00
    mov di, VBE_INFO_BUF
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done

    ; Ler ponteiro far (segmento:offset) para a lista de modos
    mov ax, [VBE_INFO_BUF + 16]     ; Segmento da lista de modos
    mov fs, ax
    mov si, [VBE_INFO_BUF + 14]     ; Offset da lista de modos

.mode_loop:
    mov cx, [fs:si]                 ; Ler numero do modo
    cmp cx, 0xFFFF
    je .vbe_done                    ; Fim da lista

    push si
    mov ax, 0x4F01                  ; Obter informacoes do modo VBE
    mov di, VBE_MODE_BUF
    int 0x10
    pop si

    cmp ax, 0x004F
    jne .next_mode

    ; Verificar se suporta LFB (Linear Frame Buffer) e modo grafico
    mov ax, [VBE_MODE_BUF]          ; Mode Attributes
    test ax, 0x0080                 ; LFB disponivel?
    jz .next_mode
    test ax, 0x0010                 ; Modo grafico?
    jz .next_mode

    ; Ler resolucao e BPP
    mov ax, [VBE_MODE_BUF + 18]     ; Width
    mov dx, [VBE_MODE_BUF + 20]     ; Height
    mov bl, [VBE_MODE_BUF + 25]     ; Bits per pixel

    ; Prioridade 1: 800x600x32
    cmp ax, 800
    jne .check_1024
    cmp dx, 600
    jne .check_1024
    cmp bl, 32
    je .found_mode

.check_1024:
    ; Prioridade 2: 1024x768x32
    cmp ax, 1024
    jne .check_640
    cmp dx, 768
    jne .check_640
    cmp bl, 32
    je .found_mode

.check_640:
    ; Prioridade 3: 640x480x32
    cmp ax, 640
    jne .next_mode
    cmp dx, 480
    jne .next_mode
    cmp bl, 32
    je .found_mode

.next_mode:
    add si, 2                       ; Proximo modo (word)
    jmp .mode_loop

.found_mode:
    ; Guardar dados VBE detetados em 0x500 para o kernel usar DEPOIS
    mov eax, [VBE_MODE_BUF + 0x28]  ; PhysBasePtr (Framebuffer)
    mov [VBE_DATA_ADDR + 0], eax

    movzx eax, word [VBE_MODE_BUF + 18] ; Width
    mov [VBE_DATA_ADDR + 4], eax

    movzx eax, word [VBE_MODE_BUF + 20] ; Height
    mov [VBE_DATA_ADDR + 8], eax

    movzx eax, word [VBE_MODE_BUF + 16] ; Pitch (Bytes por scanline)
    mov [VBE_DATA_ADDR + 12], eax

    movzx eax, byte [VBE_MODE_BUF + 25] ; BPP
    mov [VBE_DATA_ADDR + 16], eax
    
    ; IMPORTANTE: Guardamos tambem o numero do modo VBE em [VBE_DATA_ADDR + 20] 
    ; para que o kernel possa ativá-lo via BIOS ou rotina quando quiseres!
    mov [VBE_DATA_ADDR + 20], cx

.vbe_done:
    popad
    ret

; =========================
; Leitura de disco (LBA)
; EAX = LBA, CX = setores, ES:BX = destino
; =========================
read_sectors:
    pushad
    mov [dap_lba], eax
    mov [dap_count], cx
    mov [dap_off], bx
    mov [dap_seg], es
    mov di, 3
.retry:
    mov si, dap
    mov dl, [BOOT_DRIVE]
    mov ah, 0x42
    int 0x13
    jnc .ok
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13
    dec di
    jnz .retry
    jmp disk_error
.ok:
    popad
    ret

disk_error:
    mov si, error_msg
fatal:
    call print_string
    jmp $

; =========================
; A20 (metodo rapido)
; =========================
enable_a20:
    in al, 0x92
    test al, 2
    jnz .done
    or al, 2
    and al, 0xFE
    out 0x92, al
.done:
    ret

; =========================
; Print simples (BIOS)
; =========================
print_string:
    mov ah, 0x0E
    xor bx, bx
.print_loop:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .print_loop
.done:
    ret

; =========================
; Entrar em modo protegido
; =========================
enter_protected_mode:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:pm_start

; =========================
; GDT (modelo flat 4 GB)
; =========================
gdt_start:
    dq 0                        ; descritor nulo
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; =========================
; Dados
; =========================
BOOT_DRIVE   db 0
SPC          db 0
part_ptr     dw 0
PART_LBA     dd 0
FAT_LBA      dd 0
DATA_LBA     dd 0
ROOT_CLUSTER dd 0
CUR_CLUSTER  dd 0
KERNEL_CLUSTER dd 0
KERNEL_SIZE  dd 0

kernel_name db "KERNEL  BIN"

msg_stage2 db "Estagio 2 carregado (VBE detetado, modo texto preservado).", 13, 10, 0
error_msg  db "Erro ao ler o disco!", 0
part_msg   db "Particao FAT32 nao encontrada!", 0
fs_msg     db "FAT32 invalido!", 0
knf_msg    db "KERNEL.BIN nao encontrado!", 0
big_msg    db "Kernel grande demais!", 0

dap:
    db 0x10, 0
dap_count: dw 0
dap_off:   dw 0
dap_seg:   dw 0
dap_lba:   dd 0
           dd 0

; =========================
; Modo protegido 32 bits
; =========================
[bits 32]
pm_start:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; Copia o kernel de 0x10000 para 1 MB
    mov esi, KERNEL_TMP_ADDR
    mov edi, KERNEL_LOAD_ADDR
    mov ecx, [KERNEL_SIZE]
    add ecx, 3
    shr ecx, 2
    cld
    rep movsd

    movzx edx, byte [BOOT_DRIVE]    ; DL = drive de boot para o kernel
    mov eax, KERNEL_LOAD_ADDR
    jmp eax

; =========================
; Padding: exatamente STAGE2_SECTORS setores
; =========================
times STAGE2_SECTORS * 512 - ($ - $$) db 0

