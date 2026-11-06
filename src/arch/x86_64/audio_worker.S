.text
.global xapi_audio_spawn
.type xapi_audio_spawn,@function
// long spawn(stack_top, entry, argument, child_tid)
xapi_audio_spawn:
    andq $-16, %rdi
    subq $16, %rdi
    movq %rsi, 0(%rdi)
    movq %rdx, 8(%rdi)
    movq %rdi, %rsi
    movq %rcx, %r10
    xorl %edx, %edx
    xorl %r8d, %r8d
    // VM | FS | FILES | SIGHAND | THREAD | CHILD_SETTID | CHILD_CLEARTID
    movl $0x01210f00, %edi
    movl $56, %eax
    syscall
    testq %rax, %rax
    jz .Laudio_child
    ret
.Laudio_child:
    popq %rax
    popq %rdi
    call *%rax
    xorl %edi, %edi
    movl $60, %eax
    syscall
    ud2
.size xapi_audio_spawn,.-xapi_audio_spawn
.section .note.GNU-stack,"",@progbits
