// process.c - user process
//
#include "process.h"
#include "elf.h"
#include "thread.h"
#include "memory.h"
#include "console.h"

#ifdef PROCESS_TRACE
#define TRACE
#endif

#ifdef PROCESS_DEBUG
#define DEBUG
#endif


// COMPILE-TIME PARAMETERS
//

// NPROC is the maximum number of processes

#ifndef NPROC
#define NPROC 16
#endif

// INTERNAL FUNCTION DECLARATIONS
//

// INTERNAL GLOBAL VARIABLES
//

#define MAIN_PID 0
#define INITIALIZED 1

// The main user process struct

static struct process main_proc;

//int curprc; //pid of the current process -- this is actually not needed since the inline functions cover it

// A table of pointers to all user processes in the system

struct process * proctab[NPROC] = {
    [MAIN_PID] = &main_proc
};

// EXPORTED GLOBAL VARIABLES
//

char procmgr_initialized = 0;

// EXPORTED FUNCTION DEFINITIONS
//

/*
struct process {
    int id; // process id of this process
    int tid; // thread id of associated thread
    uintptr_t mtag; // memory space identifier
    struct io_intf * iotab[PROCESS_IOMAX];
};
*/

/*
Initializes processes globally by initializing a process 
structure for the main user process (init). 
The init process should always be assigned process ID (PID) 0.
*/

/* This fn just initialized the process manager. It is called in main so we set the id to main pid (0)
And we set the tid to the running_thread (probably main -> tid = 0)
we set mtag to main_mtag which is just the active space
We call thread_set_process to map the current thread and process id to each other
Finally we set the flag to show that our procmgr has been initialized. This doesn't show up anywhere else in our code
But could be a useful check in the future
*/
void procmgr_init(void){
    main_proc.id = MAIN_PID;    //set pid to 0
    main_proc.tid = running_thread();   //whatever thread is running, doesn't have to be main (lecture slides)
    main_proc.mtag = main_mtag; //whatever address space is active
    thread_set_process(main_proc.tid, &main_proc);
    procmgr_initialized = (char)INITIALIZED; //set flag to initialized
    //too ez
}

/*
Executes a program referred to by the I/O interface passed in as an argument. We only require a
maximum of 16 concurrent processes.
Executing a loaded program with process exec has 4 main requirements:
(a) First any virtual memory mappings belonging to other user processes should be unmapped.
(b) Then a fresh 2nd level (root) page table should be created 
and initialized with the default mappings for a user process.
(c) Next the executable should be loaded from the I/O interface provided as an argument into the
mapped pages. (Hint: elf load)
(d) Finally, the thread associated with the process needs to be started in user-mode. 
(Hint: An assembly function in thrasm.s would be useful here)
Context switching was relatively trivial when both contexts were at the same privilege level (i.e.
machine-mode to machine-mode switching or supervisor-mode to supervisor-mode switching),
but now we need to switch from a more privileged mode (supervisor-mode) to less privileged
mode (user-mode).
Doing so requires using clever tricks with supervisor-mode CSRs and supervisor-mode instructions. 
Here are some tips to consider while implementing a context switch from supervisor-mode
to user-mode
i. Consider instructions that can transition between lower-privilege modes and higher privilege
modes. Can you repurpose them for context switching purposes?
ii. If you did repurpose them for context switching purposes, what CSRs would you need to
edit so that the transition would start the thread’s start function in user-mode?
It’s a useful exercise to try to figure out how such an approach could work with the CSRs and
supervisor-mode instructions on your own. However, implementation on its own is a sufficient 
challenge and we don’t require you to figure this out. You can read Appendix C to find out how you can
carry out a context switch between user-mode to supervisor mode.
*/

/* Parameter: exeio
   Returns: -1. Always. Because it shouldn't return 
   
   The parameter exeio is used to be the argument into the elf loader, nothing else.
   
   First we unmap virtual memory so we can allocate a new page. This is done in elf load so no work needs to be done for (b)

   Then we call elf loader and save the entry ptr, then jump to user

   When we jump to user we set user stack ptr to xD000'0000 which triggers page fault so sp gets set to an actual appropriate value.

   Thread_jump_to_user calls our function in thrasm which puts us in U mode and jumps to user function (entryptr)
   */
int process_exec(struct io_intf *exeio){
    //(a)
    memory_unmap_and_free_user();

    //(b)
    // uint_fast8_t flags = PTE_X | PTE_U | PTE_R;
    // uintptr_t addr = memory_alloc_and_map_page(active_space_mtag(), flags); //no idea what to do with this addr


    //(c)
    void (*exe_entry)(void) = NULL; //start function
    int result = elf_load(exeio, &exe_entry);
    if (result < 0) {
        kprintf("%s: ELF load returned Error %d\n", -result);
    }
    
    //(d)
    uintptr_t sp = USER_STACK_VMA;

    thread_jump_to_user(sp, (uintptr_t)exe_entry); // according to processes slides
    return -1;

}

/*
Cleans up after a finished process by reclaiming the resources of the process. Anything that was
associated with the process at initial execution should be released. This covers:
• Process memory space
• Open I/O interfaces
• Associated kernel thread
*/

/* This function has no parameters and returns nothing. 
All it does is close memory space, io interfaces, and running thread */
void process_exit(void){
    struct process * proc = current_process();

    //close memory space
    memory_space_reclaim();

    //close all io interfaces
    for(int i = 0; i < PROCESS_IOMAX; i++){
        struct io_intf * io = proc->iotab[i];
        if(io != NULL){
            io->ops->close(io); //close io interface
        }
    }

    // recycle_thread(proc->tid); //close thread

    //process_terminate(proc->tid); - piazza says we DON'T need to use this
    thread_exit();                  //piazza says to call this 

}

//these next 2 are so free don't worry about them
//sike they did them in process.h

/*
Returns the process struct associated with the currently running thread.

struct process *current_process(void){
    return proctab[(current_pid())];
}

Returns the process ID of the process associated with the currently running thread.

int current_pid(void){
    return curprc;
}

*/