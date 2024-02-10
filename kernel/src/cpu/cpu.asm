section .text

global setGdt
global setIdt

; rdi:  gdt limit 
; rsi:  gdt base
; dx:  code segment descriptor index
; cx:  data segment descriptor index
; r8:  task segment descriptor index  
setGdt:
    push    rbp
    mov     rbp, rsp
    sub     rsp, 2 + 8

    dec     rdi ; Size minus 1
    mov     [rbp-10], di
    mov     [rbp-8], rsi
    lgdt    [rbp-10]

    ; reload cs using a far return
    shl     dx, 3
    push    dx
    lea     rax, [rel .reloadCS]
    push    rax
    retfq
.reloadCS:
    shl     cx, 3
    mov     ds, cx
    mov     es, cx
    mov     fs, cx
    mov     gs, cx
    mov     ss, cx

    ; Load task register
    shl     r8w, 3
    ltr     r8w

    mov     rsp, rbp
    pop     rbp
    ret

; rdi:  idt limit 
; rsi:  idt base
setIdt:
    push    rbp
    mov     rbp, rsp
    sub     rsp, 2 + 8

    dec     rdi ; Size minus 1
    mov     [rbp-10], di
    mov     [rbp-8], rsi
    lidt    [rbp-10]

    mov     rsp, rbp
    pop     rbp
    ret
