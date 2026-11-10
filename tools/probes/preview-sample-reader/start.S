.syntax unified
.arm
.global _start
.type _start,%function
_start:
    mov r0, sp
    bl c_entry
    mov r7, #1
    svc #0
1:
    b 1b
.size _start, .-_start
