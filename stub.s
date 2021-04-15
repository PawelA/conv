global stub_call32

BITS 64
stub_call32:
        push    rbx
        push    rbp
        push    r12
        push    r13
        push    r14
        push    r15

        sub     esp, 0x20
        mov     [rsp+8], edi
        mov     [rsp+12], esi

        lea     ecx, [rel .32]
        mov     dword [rsp], ecx
        mov     dword [rsp+4], 0x23
        jmp far [rsp]

BITS 32
.32:
        add     esp, 8
        push    0x2b
        pop     ds
        push    0x2b
        pop     es
        call    f
        mov     ecx, eax

        call    .n
.n:     add     dword [esp], .64 - .n
        mov     dword [esp+4], 0x33
        jmp far [esp]

BITS 64
.64:
        mov     eax, ecx
        add     esp, 0x1c
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbp
        pop     rbx
        ret

BITS 32
stub_call64:
        push    edi
        push    esi
        sub     esp, 4

        call    .n
.n:     add     dword [esp], .64 - .n
        mov     dword [esp+4], 0x33
        jmp far [esp]

BITS 64
.64:
        add     rsp, 4
        mov     edi, [rsp+16]

        call    g

        mov     rdx, rax
        shr     rdx, 32

        sub     rsp, 4
        lea     ecx, [rel .32]
        mov     dword [rsp], ecx
        mov     dword [rsp+4], 0x23
        jmp far [rsp]

BITS 32
.32:
        add     esp, 8
        pop     esi
        pop     edi
        ret

; functions to test, compiled with gcc

BITS 32
; f(x, y) = g(x) + g(y)
f:
        push    ebx
        sub     esp, 20
        push    dword [esp+28]
        call    stub_call64
        mov     ebx, eax        
        pop     eax
        push    dword [esp+32]
        call    stub_call64
        add     esp, 24
        add     eax, ebx
        pop     ebx
        ret
BITS 64
; g(x) = x + 1
g:
        lea     eax, [rdi+1]
        ret
