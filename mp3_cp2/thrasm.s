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

        li a0, 1
        slli a0, a0, 8
        csrc sstatus, a0
        .endm

        .macro  set_sstatus_SPIE
        # Loads a bit mask to 1 << 5 in register a0
        # sets bit 5 of sstatus

        li a0, 1
        slli a0, a0, 5
        csrs sstatus, a0
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

        


# Statically allocated stack for the idle thread.

        .section        .data.stack, "wa", @progbits
        .balign          16
        
        .equ            IDLE_STACK_SIZE, 1024

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