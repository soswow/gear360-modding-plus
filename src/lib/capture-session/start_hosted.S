/*
 * The startup code for the camera build.
 *
 * The camera's C library is installed without its headers or its startup
 * objects, so the program supplies its own.
 *
 * The contract implemented here is the one the camera's own stock
 * application uses.  That application's _start (entry 0x77d18, stock
 * application sha256
 * 5937b3abe7630abc8ac265ce280cc45ee591562968296bc4b368cf7e15742789,
 * read through inputs/stock-app-entry-ins.json) is twelve instructions:
 *
 *     0x77d18: mov  r11, #0x0
 *     0x77d1c: mov  lr, #0x0
 *     0x77d20: ldr  r1, [sp],#0x4      the count is the word at the stack
 *     0x77d24: cpy  r2, sp             the vector is the stack after it
 *     0x77d28: str  r2, [sp,#-0x4]!    push the vector
 *     0x77d2c: str  r0, [sp,#-0x4]!    push the loader's entry r0
 *     0x77d30: ldr  r12, [0x77d48]     the fini function
 *     0x77d34: str  r12, [sp,#-0x4]!   push fini
 *     0x77d38: ldr  r0, [0x77d4c]      main
 *     0x77d3c: ldr  r3, [0x77d50]      init
 *     0x77d40: bl   0x000769bc         __libc_start_main
 *
 * So, at the call:
 *
 *   r0     main
 *   r1     the argument count, loaded from the word at the entry stack
 *          pointer.  It is a number, not a pointer to the stack: the
 *          camera's C library hands it straight to main.
 *   r2     the argument vector, the stack after that word
 *   r3     the init function
 *   sp+0   the fini function
 *   sp+4   the loader's own entry r0, which the camera's C library
 *          registers with __cxa_atexit when it is not null
 *   sp+8   the argument vector again, the stock's stack-end convention
 *
 * The library's own decompilation (inputs/libc-start-pseudo.json) shows
 * six parameters that it reads, the sixth being the one it registers
 * with __cxa_atexit.  That does not settle whether the call takes a
 * seventh argument: a decompilation that reads six parameters is not a
 * proof that there is no seventh.  What is settled is the layout above,
 * because it is what the stock application does and the stock application
 * runs on the camera.
 *
 * Two of the arguments are null here, and only because this executable
 * has no constructors and no destructors of its own: the build produces
 * no .init_array and no .fini_array, so the loader has nothing of ours
 * to run, and init and fini may be null.  If either array ever gains an
 * entry, the matching argument must not be null.
 *
 * The stack pointer is 8-byte aligned at the call, as the calling
 * convention requires.  The stock's three words alone would leave it
 * four bytes out, so a fourth word is reserved above them.
 */
    .text
    .globl _start
    .type _start, %function
_start:
    mov   r11, #0                /* there is no frame here              */
    mov   r4, r0                 /* the loader's entry r0, kept for the  */
                                 /* runtime finaliser                    */
    ldr   r1, [sp], #4           /* the count, AT the stack pointer;     */
                                 /* the stack then points at the vector  */
    mov   r2, sp                 /* the argument vector                  */
    mov   r3, #0                 /* init: null, and see above            */
    ldr   r0, =main              /* main                                */
    /*
     * The stack words, in the order the stock puts them: the vector
     * ends up at sp+8, the loader's entry r0 at sp+4, and the fini
     * function at sp+0.
     */
    bic   r12, sp, #7            /* an 8-byte aligned stack pointer      */
    sub   sp, r12, #16           /* three words and a word of padding   */
    mov   r12, sp
    str   r3, [r12, #0]          /* the fini function: null             */
    str   r4, [r12, #4]          /* the loader's entry r0               */
    str   r2, [r12, #8]          /* the argument vector                 */
    bl    __libc_start_main
    /*
     * The library does not return.  If it does, leave with exit_group:
     * ARM's exit_group is 248.
     */
    mov   r0, #127
    mov   r7, #248
    svc   #0
    .size _start, .-_start
    .ltorg

    .section .note.GNU-stack,"",%progbits
