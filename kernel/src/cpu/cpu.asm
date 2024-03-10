section .text

global setGdt
global setIdt
global notifyEndOfInterrupt

MasterPicCommandPort    equ     0x20
SlavePicCommandPort     equ     0xa0
PicCommandEOI           equ     0x20
PicCommandReadISR       equ     0x0b

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

; dl:  IRQ 
; return: boolaean indicating if IRQ is spurious
notifyEndOfInterrupt:
;   Check for IRQ 7 or 15, which might be spurious.   
    mov     al, dl
    and     al, 7    
    cmp     al, 7
    jne     .no_spurious
;   Possible spurious IRQ - query ISR (In Service Register)    
    mov     al, PicCommandReadISR
    out     MasterPicCommandPort, al
    out     SlavePicCommandPort, al
    in      al, MasterPicCommandPort
    mov     ah, al   
    in      al, SlavePicCommandPort
    or      al, ah
    bt      ax, 7
    jc     .no_spurious     
;   Spurious IRQ, nothing to do
    mov     rax, 1
    ret

.no_spurious:
;   If IRQ came from Slave PIC, issue an EOI (End of Interrupt) to it
    mov     al, PicCommandEOI
    cmp     dl, 8
    jl      .send_master
    out     SlavePicCommandPort, al
.send_master:
    out     MasterPicCommandPort, al
    xor     rax, rax
    ret


