; video.asm - modo texto direto na memoria VGA
txt_boss db " BossOS ", 0

desenhar_fundo_azul:
    push eax
    push ecx
    push edi
    mov edi, VGA_MEM
    mov ecx, VGA_COLS * VGA_ROWS
    mov ax, 0x1F20          ; espaco, branco sobre azul
    rep stosw
    pop edi
    pop ecx
    pop eax
    ret

desenhar_barra_tarefas:
    push eax
    push ecx
    push esi
    push edi
    mov edi, VGA_MEM + (VGA_ROWS - 1) * VGA_COLS * 2
    mov ecx, VGA_COLS
    mov ax, 0x7020          ; espaco, preto sobre cinza
    rep stosw
    mov esi, txt_boss
    mov edi, VGA_MEM + (VGA_ROWS - 1) * VGA_COLS * 2
    mov ah, 0x70
    call vga_print
    pop edi
    pop esi
    pop ecx
    pop eax
    ret

; ESI = string, EDI = endereco na VGA, AH = cor
vga_print:
.loop:
    lodsb
    test al, al
    jz .done
    stosw
    jmp .loop
.done:
    ret
