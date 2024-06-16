 ; As long as we do not have our own linker, add a GNU-stack note to make "ld" shup up.
section .note.GNU-stack noalloc noexec nowrite progbits

section .text

global syscall

; rdi:  syscall id
syscall:
    push    r11
    push    rcx
    syscall
    pop     rcx
    pop     r11
    ret
