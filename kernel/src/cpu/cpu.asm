; As long as we do not have our own linker, add a GNU-stack note to make "ld" shup up.
section .note.GNU-stack noalloc noexec nowrite progbits

FlagsKernelMode         equ     1

struc Context
    .rflags             resq    1 ; RFLAGS register
    .cr3                resq    1 ; Control processor register
    .rip                resq    1 ; Instruction pointer
    .rbx                resq    1
    .rsp                resq    1 ; Stack pointer
    .rbp                resq    1
    .r12                resq    1
    .r13                resq    1
    .r14                resq    1
    .r15                resq    1
    .flags              resw    1 ; Context flags
endstruc

struc Core
    .kernelStack        resq    1
    .activeContext      resq    1 
endstruc

section .text

global setGdt
global setIdt
global notifyEndOfInterrupt
global initializePIC
global switchContext
global setupSyscallHandler

extern systemCallHandler

MasterPicCommandPort    equ     0x20
MasterPicDataPort       equ     MasterPicCommandPort + 1
SlavePicCommandPort     equ     0xa0
SlavePicDataPort        equ     SlavePicCommandPort + 1
PicCommandEOI           equ     0x20
PicCommandInit          equ     0x11
PicCommandReadISR       equ     0x0b
ICW4_8086               equ     0x01

; di:  gdt limit 
; rsi:  gdt base
; dx:   code segment descriptor index
; cx:   task segment descriptor index  
setGdt:
    push    rbp
    mov     rbp, rsp
    sub     rsp, 2 + 8

    dec     di      ; Size minus 1
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
    add     dx, 8
    mov     ds, dx
    mov     es, dx
    mov     fs, dx
    mov     ss, dx

    ; Load task register
    shl     cx, 3
    ltr     cx

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

; rdi: target context
switchContext:
    ; save active context
    mov     rsi, qword [gs:Core.activeContext]
    mov     rax, cr3
    mov     [rsi + Context.cr3], rax
    pop     qword [rsi + Context.rip]   ; We are jumping out of this function
    or      qword [rsi + Context.flags], FlagsKernelMode      
    mov     [rsi + Context.rbx], rbx
    mov     [rsi + Context.rsp], rsp
    mov     [rsi + Context.rbp], rbp
    mov     [rsi + Context.r12], r12
    mov     [rsi + Context.r13], r13
    mov     [rsi + Context.r14], r14
    mov     [rsi + Context.r15], r15
    pushfq
    pop     qword [rsi + Context.rflags]
    jmp     loadContext


; rdi: target context
loadContext:
    mov     qword [gs:Core.activeContext], rdi

    mov     rbx, [rdi + Context.rbx]
    mov     rbp, [rdi + Context.rbp]
    mov     r12, [rdi + Context.r12]
    mov     r13, [rdi + Context.r13]
    mov     r14, [rdi + Context.r14]
    mov     r15, [rdi + Context.r15]
    mov     rsp, [rdi + Context.rsp]
    mov     rbp, [rdi + Context.rbp]
    mov     rax, [rdi + Context.cr3]
    mov     cr3, rax

    test    dword [rdi + Context.flags], FlagsKernelMode
    jz      .return_to_user_mode
    ; Stay in kernel mode
    push    qword [rdi + Context.rflags]  ; Restore rflags 
    popfq   
    jmp     [rdi + Context.rip]

.return_to_user_mode:
    mov     rcx, [rdi + Context.rip]
    mov     r11, [rdi + Context.rflags]
    o64 sysret

; rdi:  receiver id
; rsi:  message size
; rdx:  param 1
; rcx:  param 2
; r8:   param 3
; r9:   param 4
systemCallThunk:
    mov     rax, qword [gs:Core.activeContext]
    and     dword [rax + Context.flags], !FlagsKernelMode
    mov     [rax + Context.rflags], r11
    mov     [rax + Context.rbx], rbx
    mov     [rax + Context.rbp], rbp
    mov     [rax + Context.rsp], rsp
    mov     [rax + Context.r12], r12
    mov     [rax + Context.r13], r13
    mov     [rax + Context.r14], r14
    mov     [rax + Context.r15], r15

    mov     rsp, qword [gs:Core.kernelStack]
    call    systemCallHandler

    ; After processing the system call by sending a message, we typically switch to the kernel to reduce latency
    mov     rdi, rax
    jmp     loadContext

; di kernel mode code segment descriptor index
; si user mode code segment descriptor index
; rdx core specific kernel data
setupSyscallHandler:
    push    rdx                     ; Save core specific data

    mov     ecx, 0xC0000081         ; IA32_STAR MSR address
    xor     edx, edx
    xor     eax, eax
    mov     dx, di                  ; Put kernel mode code segment index in IA32_STAR[47:32]
    dec     esi                     ; Sysret offsets by one selector for ss and two for cs 
    shl     esi, 16
    or      edx, esi                ; Put user mode code segment index in IA32_STAR[63:48]
    shl     edx, 3                  ; Convert indices into segment selectors
    wrmsr

    mov     ecx, 0xC0000082         ; IA32_LSTAR MSR address
    lea     rax, [systemCallThunk]  ; Address of syscall handler
    mov     rdx, rax
    shr     rdx, 32
    wrmsr                           ; Write to IA32_LSTAR

    ; SECURITY RISK - EVALUATE
    mov     ecx, 0xC0000084         ; IA32_FMASK MSR address
    xor     edx, edx                ; We do not mask any flags -- for now
    wrmsr                           ; Write to IA32_FMASK
    
    mov     ecx, 0xC0000080         ; IA32_EFER MSR address
    rdmsr                           
    or      eax, 0x1                ; Set SCE bit
    wrmsr                           ; Write to IA32_EFER

    pop     rdx                     ; Restore core specific data
    mov     ecx, 0xC0000101         ; Address of IA32_GS_BASE MSR
    mov     eax, edx                
    shr     rdx, 32                 
    wrmsr                           ; Write to IA32_GS_BASE
    
    ret
