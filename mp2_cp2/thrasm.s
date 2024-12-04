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
#      void (*start)(void * arg),       in a2
#      void * arg)                      in a3
#
# Sets up the initial context for a new thread. The thread will begin execution
# in /start/, receiving /arg/ as the first argument. 

# struct thread_context {
#     uint64_t s[12];           #this is of size 12*8 = 96 bytes
#     void (*ra)(uint64_t);
#     void * sp;
# };

# struct thread {
#     struct thread_context context; // this is of size 112 bytes (96+8+8)
#     enum thread_state state;
#     int id;
#     const char * name;
#     void * stack_base;
#     size_t stack_size;
#     struct thread * parent;
#     struct thread * list_next;
#     struct condition * wait_cond;
#     struct condition child_exit;
# };

#Setup the thread context
# This function in threasm.s should initialize the thread context structure, which is the first member of struct
# thread, but should not start executing the thread. The initial stack pointer and the entry function for the new
# thread are given by the sp and start arguments. The thread setup function should arrange for the thread
# to start execution in start with arg as the first argument when it is scheduled (it is passed to thread swtch
# as the argument). Furthermore, thread setup should arrange it so that returning from start is equivalent to
# calling thread exit. Hint: Examine carefully the two places where this function is called to understand how it
# is used.
_thread_setup:
        # FIXME your code goes here
        sd a1, 13*8(a0) #store sp in the current thread context struct
        sd a2, 8(a0)    #store function pointer in s1
        sd a3, 16(a0)   #store arg in s2
        la t0, exit
        sd t0, 12*8(a0) #store exit function as ra

        ret     

exit:
        la ra, thread_exit  # put thread_exit as ra  
        #when the function of our new thread finished, it goes to thread_exit
        mv a0, s2       #put arg in a0
        jr s1           #call function from thread_setup args
        #this ret command will never be hit 
        #because ra doesn't come back to this function
        #it's just here for safety
        ret


# Statically allocated stack for the idle thread.

        .section        .data.idle_stack
        .align          16
        
        .equ            IDLE_STACK_SIZE, 1024
        .equ            IDLE_GUARD_SIZE, 0

        .global         _idle_stack
        .type           _idle_stack, @object
        .size           _idle_stack, IDLE_STACK_SIZE

        .global         _idle_guard
        .type           _idle_guard, @object
        .size           _idle_guard, IDLE_GUARD_SIZE

_idle_stack:
        .fill   IDLE_STACK_SIZE, 1, 0xA5

_idle_guard:
        .fill   IDLE_GUARD_SIZE, 1, 0x5A
        .end

