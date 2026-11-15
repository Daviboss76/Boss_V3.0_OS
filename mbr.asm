; mbr.asm
; Montar com: nasm -f bin mbr.asm -o mbr.bin

[bits 16]
[org 0x0600]                ; o MBR se relocará para 0x0600

STAGE2_OFFSET  equ 0x1000   ; Onde o estágio 2 será carregado
STAGE2_SECTORS equ 50       ; Quantos setores ler (ajuste conforme seu estágio 2)
STAGE2_LBA     equ 1        ; Setor logo após o MBR

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; Relocar MBR: 0x7C00 -> 0x0600 (libera 0x7C00 p/ o estágio 2/VBR)
    cld
    mov si, 0x7C00
    mov di, 0x0600
    mov cx, 256             ; 256 words = 512 bytes
    rep movsw
    jmp 0x0000:main         ; continua executando a cópia

main:
    mov [BOOT_DRIVE], dl    ; BIOS coloca o drive em DL

    call check_lba
    call load_stage2
    call jump_to_stage2

hang:
    jmp hang

; =========================
; Verificar suporte a LBA (INT 13h estendido)
; =========================
check_lba:
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc no_lba
    cmp bx, 0xAA55
    jne no_lba
    ret

no_lba:
    mov si, lba_msg
    call print_string
    jmp $

; =========================
; Carregar estágio 2 do disco
; =========================
load_stage2:
    mov di, 3               ; tentativas

.retry:
    mov si, dap             ; DS:SI -> Disk Address Packet
    mov dl, [BOOT_DRIVE]
    mov ah, 0x42            ; função BIOS: leitura estendida (LBA)
    int 0x13
    jnc .done

    xor ax, ax              ; reset do disco e tenta de novo
    mov dl, [BOOT_DRIVE]
    int 0x13
    dec di
    jnz .retry
    jmp disk_error

.done:
    ret

disk_error:
    mov si, error_msg
    call print_string
    jmp $

; =========================
; Pular para o estágio 2
; =========================
jump_to_stage2:
    mov dl, [BOOT_DRIVE]    ; estágio 2 recebe o drive em DL
    mov si, 0x0600 + 446    ; e a tabela de partições em DS:SI
    jmp 0x0000:STAGE2_OFFSET

; =========================
; Print simples (BIOS)
; =========================
print_string:
    mov ah, 0x0E
    xor bx, bx              ; página 0
.print_loop:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .print_loop
.done:
    ret

; =========================
; Dados
; =========================
BOOT_DRIVE db 0

error_msg db "Erro ao ler o disco!", 0
lba_msg   db "BIOS sem suporte a LBA!", 0

; Disk Address Packet
dap:
    db 0x10                 ; tamanho do pacote
    db 0                    ; reservado
    dw STAGE2_SECTORS       ; setores a ler
    dw STAGE2_OFFSET        ; offset de destino
    dw 0x0000               ; segmento de destino
    dd STAGE2_LBA           ; LBA (32 bits baixos)
    dd 0                    ; LBA (32 bits altos)

; =========================
; Padding + tabela de partições + assinatura
; =========================
times 446 - ($ - $$) db 0   ; falha na montagem se passar de 446 bytes

partition_table:
    times 64 db 0           ; 4 entradas de 16 bytes (preencha se precisar)

dw 0xAA55
