# thrasm.s - Special functions called from thread.c
#

# struct thread * _thread_swtch(struct thread * resuming_thread)

# Switches from the currently running thread to another thread and returns when
# the current thread is scheduled to run again. Argument /resuming_thread/ is
# the thread to be resumed. Returns a pointer to the previously-scheduled
# thread. This function is called in thread.c. The spelling of swtch is
# historic.

        .text
        .global _thread_swtch
        .type   _thread_swtch, @function

        .macro  clear_sstatus_SPP
        # Loads a bit mask to 1 << 8 in register a0
        # clears bit 8 of sstatus

        li t6, 1
        slli t6, t6, 8
        csrc sstatus, t6
        .endm

        .macro  set_sstatus_SPIE
        # Loads a bit mask to 1 << 5 in register a0
        # sets bit 5 of sstatus

        li t6, 1
        slli t6, t6, 5
        csrs sstatus, t6
        .endm

        .macro  restore_sstatus_and_sepc
        # Restores sstatus and sepc from trap frame to which sp points. We use
        # t6 as a temporary, so must be used after this macro, not before.

        ld      t6, 33*8(sp)
        csrw    sepc, t6
        ld      t6, 32*8(sp)
        csrw    sstatus, t6
        .endm


        .macro  restore_gprs_except_t6_and_sp

        ld      x30, 30*8(sp)   # x30 is t5
        ld      x29, 29*8(sp)   # x29 is t4
        ld      x28, 28*8(sp)   # x28 is t3
        ld      x27, 27*8(sp)   # x27 is s11
        ld      x26, 26*8(sp)   # x26 is s10
        ld      x25, 25*8(sp)   # x25 is s9
        ld      x24, 24*8(sp)   # x24 is s8
        ld      x23, 23*8(sp)   # x23 is s7
        ld      x22, 22*8(sp)   # x22 is s6
        ld      x21, 21*8(sp)   # x21 is s5
        ld      x20, 20*8(sp)   # x20 is s4
        ld      x19, 19*8(sp)   # x19 is s3
        ld      x18, 18*8(sp)   # x18 is s2
        ld      x17, 17*8(sp)   # x17 is a7
        ld      x16, 16*8(sp)   # x16 is a6
        ld      x15, 15*8(sp)   # x15 is a5
        ld      x14, 14*8(sp)   # x14 is a4
        ld      x13, 13*8(sp)   # x13 is a3
        ld      x12, 12*8(sp)   # x12 is a2
        ld      x11, 11*8(sp)   # x11 is a1
        ld      x10, 10*8(sp)   # x10 is a0
        ld      x9, 9*8(sp)     # x9 is s1
        ld      x8, 8*8(sp)     # x8 is s0/fp
        ld      x7, 7*8(sp)     # x7 is t2
        ld      x6, 6*8(sp)     # x6 is t1
        ld      x5, 5*8(sp)     # x5 is t0
        ld      x4, 4*8(sp)     # x4 is tp
        ld      x3, 3*8(sp)     # x3 is gp
        ld      x1, 1*8(sp)     # x1 is ra

        .endm

_thread_swtch:

        # We only need to save the ra and s0 - s12 registers. Save them on
        # the stack and then save the stack pointer. Our declaration is:
        # 
        #   struct thread * _thread_swtch(struct thread * resuming_thread);
        #
        # The currently running thread is suspended and resuming_thread is
        # restored to execution. swtch returns when execution is switched back
        # to the calling thread. The return value is the previously executing
        # thread. Interrupts are enabled when swtch returns.
        #
        # tp = pointer to struct thread of current thread (to be suspended)
        # a0 = pointer to struct thread of thread to be resumed
        # 

        sd      s0, 0*8(tp)
        sd      s1, 1*8(tp)
        sd      s2, 2*8(tp)
        sd      s3, 3*8(tp)
        sd      s4, 4*8(tp)
        sd      s5, 5*8(tp)
        sd      s6, 6*8(tp)
        sd      s7, 7*8(tp)
        sd      s8, 8*8(tp)
        sd      s9, 9*8(tp)
        sd      s10, 10*8(tp)
        sd      s11, 11*8(tp)
        sd      ra, 12*8(tp)
        sd      sp, 13*8(tp)

        mv      tp, a0

        ld      sp, 13*8(tp)
        ld      ra, 12*8(tp)
        ld      s11, 11*8(tp)
        ld      s10, 10*8(tp)
        ld      s9, 9*8(tp)
        ld      s8, 8*8(tp)
        ld      s7, 7*8(tp)
        ld      s6, 6*8(tp)
        ld      s5, 5*8(tp)
        ld      s4, 4*8(tp)
        ld      s3, 3*8(tp)
        ld      s2, 2*8(tp)
        ld      s1, 1*8(tp)
        ld      s0, 0*8(tp)
                
        ret

        .global _thread_setup
        .type   _thread_setup, @function

# void _thread_setup (
#      struct thread * thr,             in a0
#      void * sp,                       in a1
#      void (*start)(void *, void *),   in a2
#      void * arg)                      in a3
#
# Sets up the initial context for a new thread. The thread will begin execution
# in /start/, receiving the five arguments passed to _thread_set after /start/.

_thread_setup:
        # Write initial register values into struct thread_context, which is the
        # first member of struct thread.
        
        sd      a1, 13*8(a0)    # Initial sp
        sd      a2, 11*8(a0)    # s11 <- start
        sd      a3, 0*8(a0)     # s0 <- arg 0
        sd      a4, 1*8(a0)     # s1 <- arg 1
        sd      a5, 2*8(a0)     # s2 <- arg 2
        sd      a6, 3*8(a0)     # s3 <- arg 3
        sd      a7, 4*8(a0)     # s4 <- arg 4

        # put address of thread entry glue into t1 and continue execution at 1f

        jal     t0, 1f

        # The glue code below is executed when we first switch into the new thread

        la      ra, thread_exit # child will return to thread_exit
        mv      a0, s0          # get arg argument to child from s0
        mv      a1, s1          # get arg argument to child from s0
        mv      fp, sp          # frame pointer = stack pointer
        jr      s11             # jump to child entry point (in s1)

1:      # Execution of _thread_setup continues here

        sd      t0, 12*8(a0)    # put address of above glue code into ra slot

        ret

        .global _thread_finish_jump
        .type   _thread_finish_jump, @function

# void __attribute__ ((noreturn)) _thread_finish_jump (
#      struct thread_stack_anchor * stack_anchor,
#      uintptr_t usp, uintptr_t upc, ...);

.macro restore_gprs_except_args
        ld      x31, 31*8(a1)   # x31 is t6
        ld      x30, 30*8(a1)   # x30 is t5
        ld      x28, 28*8(a1)   # x28 is t3
        ld      x27, 27*8(a1)   # x27 is s11
        ld      x26, 26*8(a1)   # x26 is s10
        ld      x29, 29*8(a1)   # x29 is t4
        ld      x25, 25*8(a1)   # x25 is s9
        ld      x24, 24*8(a1)   # x24 is s8
        ld      x23, 23*8(a1)   # x23 is s7
        ld      x22, 22*8(a1)   # x22 is s6
        ld      x21, 21*8(a1)   # x21 is s5
        ld      x20, 20*8(a1)   # x20 is s4
        ld      x19, 19*8(a1)   # x19 is s3
        ld      x18, 18*8(a1)   # x18 is s2
        ld      x17, 17*8(a1)   # x17 is a7
        ld      x16, 16*8(a1)   # x16 is a6
        ld      x15, 15*8(a1)   # x15 is a5
        ld      x14, 14*8(a1)   # x14 is a4
        ld      x13, 13*8(a1)   # x13 is a3
        ld      x12, 12*8(a1)   # x12 is a2
        # ld      x11, 11*8(a1)   # x11 is a1
        # ld      x10, 10*8(a1)   # x10 is a0
        ld      x9, 9*8(a1)     # x9 is s1
        ld      x8, 8*8(a1)     # x8 is s0/fp
        ld      x7, 7*8(a1)     # x7 is t2
        ld      x6, 6*8(a1)     # x6 is t1
        ld      x5, 5*8(a1)     # x5 is t0
        ld      x4, 4*8(a1)     # x4 is tp
        ld      x3, 3*8(a1)     # x3 is gp
        ld      x2, 2*8(a1)     # x2 is sp
        ld      x1, 1*8(a1)     # x1 is ra

.endm



_thread_finish_jump: #thread_finish_jump completes the jump to U mode
# current stack base, user stack, and user pc are passed in as arguments.
# We have to first disable interrupts since we're accessing important/shared variables
# Then save prev stack pointer in sscratch so that we can access kernel stack in trapasm
# jump to U mode, so set stvec to u mode trap handler and sret to user function
# then set user stack pointer from the argument
# Set sstatus spp and spie bits according to lecture slides to tell program we are in user
# Then restore original interrupt state (bit 1 of sstatus)
# finally call sret to go to user program. This works because we set user pc into sepc to it goes there.

        # While in user mode, sscratch points to a struct thread_stack_anchor
        # located at the base of the stack, which contains the current thread
        # pointer and serves as our starting stack pointer.

        # a0 contains curthr->stack_base
        # a1 contains user stack pointer
        # a2 contains user pc

        # TODO: FIXME your code here
        # read status and clear SIE (interrupt enable bit) (bit 1)
        csrrci a3, sstatus, 2 # b10
        andi a3, a3, 2            # read bit 1 

        # we are jumping to user mode so put previous stack pointer in sscratchs
        sd tp, 0(a0)
        csrw sscratch, a0
        la t0, _trap_entry_from_umode  # Load address of _trap_entry_from_umode into t0
        csrw stvec, t0 # set trap entry
        csrw sepc, a2                      # set sepc to return to user program on sret

        mv sp, a1 # set stack pointer

        # v these could've been done in C lol v

        #set sstatus.SPP to 0
        clear_sstatus_SPP       # clear SPP  ->  sstatus &= ~SPP

        #set sstatus.SPIE to 1
        set_sstatus_SPIE        # set SPIE  ->   sstatus |= SPIE

        csrs sstatus, a3 # Restore interrupts - set bit 1 to what it was before

        sret # go to user program and switch to U mode

    .global _thread_finish_fork
    .type   _thread_finish_fork, @function

# void _thread_finish_fork (struct thread * child, const struct trap_frame * parent_tfr);

# This function begins by saving the currently running thread. Switches to the new child process thread and
# back to the U mode interrupt handler. It then restores the ”saved” trap frame which is actually the duplicated
# parent trap frame. Be sure to set up the return value correctly, it will be different between the child and
# parent process. As always, we sret to jump into the new user process.

# parent process must return child tid, child process returns 0.

# a1 contains parent trap frame, a0 contains child proc
_thread_finish_fork:   

        # context switch
        sd      s0, 0*8(tp)
        sd      s1, 1*8(tp)
        sd      s2, 2*8(tp)
        sd      s3, 3*8(tp)
        sd      s4, 4*8(tp)
        sd      s5, 5*8(tp)
        sd      s6, 6*8(tp)
        sd      s7, 7*8(tp)
        sd      s8, 8*8(tp)
        sd      s9, 9*8(tp)
        sd      s10, 10*8(tp)
        sd      s11, 11*8(tp)
        sd      ra, 12*8(tp)
        sd      sp, 13*8(tp)

        # Switch context to the child thread
        mv      tp, a0              # Move child thread pointer to tp (current thread pointer)
        ld      sp, 13*8(tp)        # Load child thread's kernel stack pointer from tp
        sd      x0, 10*8(sp)        # set return value of the child fork process

         # Set trap handler to user mode trap entry point
        la t6, _trap_entry_from_umode   #set trap handler address to umode
        csrw stvec, t6

        restore_sstatus_and_sepc       # Restore sstatus and sepc
        restore_gprs_except_t6_and_sp  # Restore general-purpose registers

        addi    t6, sp, 34*8    # set the new ksp to the stack base
        csrw    sscratch, t6    # set sscratch to the ksp
        ld      t6, 31*8(sp)    # save t6 (x31) in trap frame
        ld      sp, 2*8(sp)     # set the usp to sp[2]

        sret                          # Return to user mode
        # this goes to where fork was called in user main function





# Statically allocated stack for the idle thread.

        .section        .data.stack, "wa", @progbits
        .balign          16
        
        .equ            IDLE_STACK_SIZE, 4096

        .global         _idle_stack_lowest
        .type           _idle_stack_lowest, @object
        .size           _idle_stack_lowest, IDLE_STACK_SIZE

        .global         _idle_stack_anchor
        .type           _idle_stack_anchor, @object
        .size           _idle_stack_anchor, 2*8

_idle_stack_lowest:
        .fill   IDLE_STACK_SIZE, 1, 0xA5

_idle_stack_anchor:
        .global idle_thread # from thread.c
        .dword  idle_thread
        .fill   8
        .end

