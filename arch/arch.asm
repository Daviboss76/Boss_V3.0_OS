[bits 32]
extern isr_handler
global gdt_load, tss_load, idt_load, enter_user, ksetjmp, klongjmp
global isr_table, isr128

section .text

gdt_load:                       ; void gdt_load(gdtr_t *p)
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush
.flush:
    ret

tss_load:
    mov ax, 0x28
    ltr ax
    ret

idt_load:                       ; void idt_load(idtr_t *p)
    mov eax, [esp + 4]
    lidt [eax]
    ret

enter_user:                     ; void enter_user(eip, esp) -> ring 3
    mov eax, [esp + 4]
    mov ecx, [esp + 8]
    mov dx, 0x23
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    push dword 0x23             ; ss
    push ecx                    ; esp
    push dword 0x202            ; eflags (IF=1, IOPL=0)
    push dword 0x1B             ; cs
    push eax                    ; eip
    iretd

ksetjmp:                        ; int ksetjmp(kjmp_t *b)
    mov edx, [esp + 4]
    mov [edx + 0], ebx
    mov [edx + 4], esi
    mov [edx + 8], edi
    mov [edx + 12], ebp
    lea ecx, [esp + 4]
    mov [edx + 16], ecx
    mov ecx, [esp]
    mov [edx + 20], ecx
    xor eax, eax
    ret

klongjmp:                       ; void klongjmp(kjmp_t *b, int v)
    mov edx, [esp + 4]
    mov eax, [esp + 8]
    test eax, eax
    jnz .ok
    inc eax
.ok:
    mov ebx, [edx + 0]
    mov esi, [edx + 4]
    mov edi, [edx + 8]
    mov ebp, [edx + 12]
    mov esp, [edx + 16]
    jmp [edx + 20]

; ---------- stubs de excecao / IRQ / syscall ----------
%macro ISR_NOERR 1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro
%macro ISR_ERR 1
isr%1:
    push dword %1
    jmp isr_common
%endmacro
%macro LISTA_NOERR 1-*
%rep %0
    ISR_NOERR %1
%rotate 1
%endrep
%endmacro
%macro LISTA_ERR 1-*
%rep %0
    ISR_ERR %1
%rotate 1
%endrep
%endmacro
%macro TABELA 1-*
%rep %0
    dd isr%1
%rotate 1
%endrep
%endmacro

LISTA_NOERR 0,1,2,3,4,5,6,7,9,15,16,18,19,20,21,22,23,24,25,26,27,28,29,31
LISTA_NOERR 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
LISTA_ERR   8,10,11,12,13,14,17,30

isr128:
    push dword 0
    push dword 0x80
    jmp isr_common

isr_common:
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld
    push esp
    call isr_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 8
    iretd

section .rodata
isr_table:
    TABELA 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    TABELA 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    TABELA 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
