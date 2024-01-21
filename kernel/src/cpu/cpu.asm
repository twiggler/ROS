section .text

global setGdt

; rdi:  gdt limit 
; rsi:  gdt base
; dx:  code segment
; cx:  data segment 
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
    lea     rax, [rel .reload_CS]
    push    rax
    retfq
.reload_CS:
    shl     cx, 3
    mov     ds, cx
    mov     es, cx
    mov     fs, cx
    mov     gs, cx
    mov     ss, cx

    mov     rsp, rbp
    pop     rbp
    ret

