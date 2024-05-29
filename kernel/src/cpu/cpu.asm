; As long as we do not have our own linker, add a GNU-stack note to make "ld" shup up.
section .note.GNU-stack noalloc noexec nowrite progbits

struc Context
    .cp3    resq    1 ; Control processor register
    .rip    resq    1 ; Instruction pointer
    .rsp    resq    1 ; Stack pointer
    .rax    resq    1 ; General purpose registers
    .rbx    resq    1
    .rcx    resq    1
    .rdx    resq    1
    .rsi    resq    1
    .rdi    resq    1
    .rbp    resq    1
    .r8     resq    1
    .r9     resq    1
    .r10    resq    1
    .r11    resq    1
    .r12    resq    1
    .r13    resq    1
    .r14    resq    1
    .r15    resq    1
endstruc

section .bss

; We only store the context of a single task for now
contextInstance resb Context_size

section .text

global setGdt
global setIdt
global notifyEndOfInterrupt
global initializePIC
global createContext
global switchContext

MasterPicCommandPort    equ     0x20
MasterPicDataPort       equ     MasterPicCommandPort + 1
SlavePicCommandPort     equ     0xa0
SlavePicDataPort        equ     SlavePicCommandPort + 1
PicCommandEOI           equ     0x20
PicCommandInit          equ     0x11
PicCommandReadISR       equ     0x0b
ICW4_8086               equ     0x01

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

; dil:   Master Vector Offset
; sil:   Slave Vector Offset
initializePIC:
    ; Send initialization command
    mov     al, PicCommandInit 
    out     MasterPicCommandPort, al
    out     SlavePicCommandPort, al

    ; Send vector offsets
    mov     al, dil
    out     MasterPicDataPort, al
    mov     al, sil
    out     SlavePicDataPort, al

    ; Wire master / slave
    mov     al, 4   
    out     MasterPicDataPort, al   ; Slave at IRQ2
    mov     al, 2   
    out     SlavePicDataPort, al    ; Cascade identity of slave

    ; Instruct PICs to use 8086 mode
    mov     al, ICW4_8086
    out     MasterPicDataPort, al
    out     SlavePicDataPort, al

    ; Set interrupt masks to keyboard only
    mov     al, 0xf9
    out     MasterPicDataPort, al
    mov     al, 0xff
    out     SlavePicDataPort, al

    ret

; dil:  IRQ 
; return: boolean indicating if IRQ is spurious
notifyEndOfInterrupt:
;   Check for IRQ 7 or 15, which might be spurious.   
    mov     al, dil
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
;   If IRQ came from Slave PIC, issue an EOI (End of Interrupt) to both slave and master
    mov     al, PicCommandEOI
    cmp     dil, 8
    jl      .send_master
    out     SlavePicCommandPort, al
.send_master:
    out     MasterPicCommandPort, al
    xor     rax, rax
    ret

; rdi:  cp3
; rsi:  rip
; rdx:  rsp
; rcx:  arg1 passed to main
createContext:
    mov     [contextInstance + Context.cp3], rdi
    mov     [contextInstance + Context.rip], rsi
    mov     [contextInstance + Context.rsp], rdx
    mov     [contextInstance + Context.rdi], rcx    ; first argument to main

    mov     eax, 1  ; Return context id
    ret

; rdi = context id
switchContext:
    ; This will become the syscall handler.
    ; For now, this routine is used to test the elf loader.
    mov     rax, [contextInstance + Context.cp3]
    mov     cr3, rax
    mov     rsp, [contextInstance + Context.rsp]
    mov     rdi, [contextInstance + Context.rdi]
    
    ; Note that we are still in ring 0.
    jmp     [contextInstance + Context.rip]


