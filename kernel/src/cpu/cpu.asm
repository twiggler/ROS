section .text

global setGdt

; rdi:  gdt limit 
; rsi:  gdt base
; rdx:  code segment
; rcx:  data segment 
setGdt:
    push    rbp
    mov     rbp, rsp
    sub     rsp, 2 + 8

    mov     [rbp-10], di
    mov     [rbp-8], rsi
    lgdt    [rbp]

    ; reload cs using a far return
    push    dx
    lea     rax, [rel .reload_CS]
    push    rax
    retfq
.reload_CS:
    mov     ds, cx
    mov     es, cx
    mov     fs, cx
    mov     gs, cx
    mov     ss, cx

    pop     rbp
    ret

