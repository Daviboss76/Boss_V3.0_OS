[bits 32]
extern kmain
extern __bss_start
extern __bss_end
global _start

section .text.entry
_start:
    cli
    cld
    ; zera o BSS (ainda na pilha do estagio 2)
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, stack_top

    movzx edx, dl           ; DL = drive de boot (vem do estagio 2)
    push edx
    call kmain
.hang:
    hlt
    jmp .hang

section .bss
align 16
stack_bottom: resb 16384
stack_top:
