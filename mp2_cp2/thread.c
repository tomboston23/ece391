// thread.c - Threads
//

#include "thread.h"

#include <stddef.h>
#include <stdint.h>

#include "halt.h"
#include "console.h"
#include "heap.h"
#include "string.h"
#include "csr.h"
#include "intr.h"

// COMPILE-TIME PARAMETERS
//

// NTHR is the maximum number of threads

#ifndef NTHR
#define NTHR 16
#endif

// Size of stack allocated for new threads.

#ifndef THREAD_STKSZ
#define THREAD_STKSZ 4096
#endif

// Size of guard region between stack bottom (highest address + 1) and thread
// structure. Gives some protection against bugs that write past the end of the
// stack, but not much.

#ifndef THREAD_GRDSZ
#define THREAD_GRDSZ 16
#endif

// EXPORTED GLOBAL VARIABLES
//

char thread_initialized = 0;

// INTERNAL TYPE DEFINITIONS
//

enum thread_state {
    THREAD_UNINITIALIZED = 0,
    THREAD_STOPPED,
    THREAD_WAITING,
    THREAD_RUNNING,
    THREAD_READY,
    THREAD_EXITED
};

struct thread_context {
    uint64_t s[12];
    void (*ra)(uint64_t);
    void * sp;
};

struct thread {
    struct thread_context context; // must be first member (thrasm.s)
    enum thread_state state;
    int id;
    const char * name;
    void * stack_base;
    size_t stack_size;
    struct thread * parent;
    struct thread * list_next;
    struct condition * wait_cond;
    struct condition child_exit;
};

// INTERNAL GLOBAL VARIABLES
//

extern char _main_stack[];  // from start.s
extern char _main_guard[];  // from start.s

#define MAIN_TID 0
#define IDLE_TID (NTHR-1)

static struct thread main_thread = {
    .name = "main",
    .id = MAIN_TID,
    .state = THREAD_RUNNING,
    .stack_base = &_main_guard,

    .child_exit = {
        .name = "main.child_exit"
    }
};

extern char _idle_stack[];  // from thrasm.s
extern char _idle_guard[];  // from thrasm.s

static struct thread idle_thread = {
    .name = "idle",
    .id = IDLE_TID,
    .state = THREAD_READY,
    .parent = &main_thread,
    .stack_base = &_idle_guard
};

static struct thread * thrtab[NTHR] = {
    [MAIN_TID] = &main_thread,
    [IDLE_TID] = &idle_thread
};

static struct thread_list ready_list;

// INTERNAL MACRO DEFINITIONS
// 

// Macro for changing thread state. If compiled for debugging (DEBUG is
// defined), prints function that changed thread state.

#define set_thread_state(t,s) do { \
    debug("Thread \"%s\" state changed from %s to %s in %s", \
        (t)->name, thread_state_name((t)->state), thread_state_name(s), \
        __func__); \
    (t)->state = (s); \
} while (0)

// Pointer to current thread, which is kept in the tp (x4) register.

#define CURTHR ((struct thread*)__builtin_thread_pointer())

// INTERNAL FUNCTION DECLARATIONS
//

// Finishes initialization of the main thread; must be called in main thread.

static void init_main_thread(void);

// Initializes the special idle thread, which soaks up any idle CPU time.

static void init_idle_thread(void);

static void set_running_thread(struct thread * thr);
static const char * thread_state_name(enum thread_state state);

// void recycle_thread(int tid)
// Reclaims a thread's slot in thrtab and makes its parent the parent of its
// children. Frees the struct thread of the thread.

static void recycle_thread(int tid);

// void suspend_self(void)
// Suspends the currently running thread and resumes the next thread on the
// ready-to-run list using _thread_swtch (in threasm.s). Must be called with
// interrupts enabled. Returns when the current thread is next scheduled for
// execution. If the current thread is RUNNING, it is marked READY and placed
// on the ready-to-run list. Note that suspend_self will only return if the
// current thread becomes READY.

static void suspend_self(void);

// The following functions manipulate a thread list (struct thread_list). Note
// that threads form a linked list via the list_next member of each thread
// structure. Thread lists are used for the ready-to-run list (ready_list) and
// for the list of waiting threads of each condition variable.

static void tlclear(struct thread_list * list);
static int tlempty(const struct thread_list * list);
static void tlinsert(struct thread_list * list, struct thread * thr);
static struct thread * tlremove(struct thread_list * list);
static void tlappend(struct thread_list * l0, const struct thread_list * l1);

static void idle_thread_func(void * arg);

// IMPORTED FUNCTION DECLARATIONS
// defined in thrasm.s
//

extern void _thread_setup (
    struct thread * thr,
    void * sp,
    void (*start)(void * arg),
    void * arg);

extern struct thread * _thread_swtch (
    struct thread * resuming_thread);

// EXPORTED FUNCTION DEFINITIONS
//

int running_thread(void) {
    return CURTHR->id;
}

void thread_init(void) {
    init_main_thread();
    init_idle_thread();
    set_running_thread(&main_thread);
    thread_initialized = 1;
}

int thread_spawn(const char * name, void (*start)(void *), void * arg) {
    struct thread * child;
    int tid;

    //trace("%s(name=\"%s\") in %s", __func__, name, running_thread()->name);

    // Find a free thread slot.

    tid = 0;
    while (++tid < NTHR)
        if (thrtab[tid] == NULL)
            break;
    
    if (tid == NTHR)
        panic("Too many threads");
    
    // Allocate a struct thread and a stack

    child = kmalloc(THREAD_STKSZ + THREAD_GRDSZ + sizeof(struct thread));
    child = (void*)child + THREAD_STKSZ + THREAD_GRDSZ;
    memset(child, 0, sizeof(struct thread));

    thrtab[tid] = child;

    child->id = tid;
    child->name = name;
    child->parent = CURTHR;
    child->stack_base = (void*)child - THREAD_GRDSZ;
    child->stack_size = THREAD_STKSZ;
    set_thread_state(child, THREAD_READY);
    _thread_setup(child, child->stack_base, start, arg);
    tlinsert(&ready_list, child);
    
    return tid;
}

void thread_exit(void) {
    //trace("thread_exit called for thread %s", CURTHR->name);
    if (CURTHR == &main_thread)
        halt_success();
    
    set_thread_state(CURTHR, THREAD_EXITED);

    // Signal parent in case it is waiting for us to exit

    assert(CURTHR->parent != NULL);
    condition_broadcast(&CURTHR->parent->child_exit);

    suspend_self(); // should not return
    panic("thread_exit() failed");
}

void thread_yield(void) {
    //trace("%s() in %s", __func__, CURTHR->name);

    assert (intr_enabled());
    assert (CURTHR->state == THREAD_RUNNING);

    suspend_self();
}

int thread_join_any(void) {
    int childcnt = 0;
    int tid;

    //trace("%s() in %s", __func__, CURTHR->name);

    // See if there are any children of the current thread, and if they have
    // already exited. If so, call thread_wait_one() to finish up.

    for (tid = 1; tid < NTHR; tid++) {
        if (thrtab[tid] != NULL && thrtab[tid]->parent == CURTHR) {
            if (thrtab[tid]->state == THREAD_EXITED)
                return thread_join(tid);
            childcnt++;
        }
    }

    // If the current thread has no children, this is a bug. We could also
    // return -EINVAL if we want to allow the calling thread to recover.

    if (childcnt == 0)
        panic("thread_wait called by childless thread");
    

    // Wait for some child to exit. An exiting thread signals its parent's
    // child_exit condition.

    condition_wait(&CURTHR->child_exit);

    for (tid = 1; tid < NTHR; tid++) {
        if (thrtab[tid] != NULL &&
            thrtab[tid]->parent == CURTHR &&
            thrtab[tid]->state == THREAD_EXITED)
        {
            recycle_thread(tid);
            return tid;
        }
    }

    panic("spurious child_exit signal");
}

// Wait for specific child thread to exit. Returns the thread id of the child.

/*Waits for a specific child thread to exit
This function in thread.c waits for a specified child thread to exit. Argument tid is the TID of the child to
wait for. If the calling thread is the parent of the specified child thread, thread join waits for the child to exit
and then returns the child TID. If there is no thread with the specified TID or the calling thread is not the parent
of the specified thread, thread join should return −1. There are two cases you must cover:
1. Child already exited. If the child has already exited, thread join does not need to wait. It should return
immediately.
2. Child still running. If the child is still running, the parent should wait on the condition variable child exit
in its own struct thread. Note that an existing child signals this condition in thread exit.
In either case, the parent should release the resources used by the child thread by calling recycle thread.
See the function thread join any for inspiration. (The thread join any function waits for any of a thread’s
children to exit.)*/

/*Function interface for thread_join:

thread_join waits for a child thread to exit. We first check tid is a child of the current thread
and that thread tid exists. Then we wait for the child to be in the EXITED state using a while loop
and condition_wait. It is a while loop in case there are multiple children and the condition is fired
for another child. (in our case, there will only be one child so it should function regardless)
Once we determine that tid is exited, recycle the thread (free it) and return 
*/
int thread_join(int tid) {
    // FIXME your goes code here;d
    //trace("thread_join tid: %d", tid);
    if(thrtab[tid] == NULL || thrtab[tid]->parent != CURTHR){ //check if tid is invalid. If it is, return -1
        return -1;
    }

    while(thrtab[tid]->state != THREAD_EXITED){      //if the child hasn't exited, wait for the child to exit
        //In the context of our demo, this represents waiting for the star trek game to terminate
        //trace("waiting for thread %s to exit", thrtab[tid]->name);
        condition_wait(&CURTHR->child_exit);    //wait for game to exit
    }
    //trace("thread %s exited", thrtab[tid]->name);

    recycle_thread(tid);//free the thread
    return tid;         //return

}

void condition_init(struct condition * cond, const char * name) {
    cond->name = name;
    tlclear(&cond->wait_list);
}

void condition_wait(struct condition * cond) {
    int saved_intr_state;
    // if(cond == &main_thread.child_exit){
    //     trace("%s(cond=<%s>) in %s", __func__, cond->name, CURTHR->name);
    // }
    

    assert(CURTHR->state == THREAD_RUNNING);

    // Insert current thread into condition wait list
    
    set_thread_state(CURTHR, THREAD_WAITING);
    CURTHR->wait_cond = cond;
    CURTHR->list_next = NULL;

    tlinsert(&cond->wait_list, CURTHR);

    saved_intr_state = intr_enable();

    suspend_self();

    intr_restore(saved_intr_state);
}

/*Causes all sleeping threads on condition to wake up*/
/*Wakes up all threads waiting on the condition variable
This function thread.c wakes up all threads waiting for a condition to be signalled. Waking up a thread entails:
– Changing its state from WAITING to READY,
– Placing it on the ready-to-run list, and
– removing it from the list of threads waiting on the condition.
After broadcasting the condition, the set of threads waiting on the condition should be empty.*/

/*Function interface for condition_broadcast:

Condition_broadcast first finds the head of the wait_list for the given condition
After determining it is valid (not NULL), it iterates through the wait list
Each node in wait list should be set to the READY state and inserted into ready list
At the end, the wait list needs to be cleared because they're not waiting on the condition anymore
*/
void condition_broadcast(struct condition * cond) {
    // FIXME your code goes here
    // if(cond == &main_thread.child_exit){
    //     trace("%s(cond=<%s>) in %s", __func__, cond->name, CURTHR->name);
    // }
    
    struct thread_list * list = &cond->wait_list;   //get wait_list for our condition
    if(list == NULL){   //This condition never gets hit but is just here for safety
        return;         //The code functions just fine without this condition
    }
    struct thread * h = list->head; //get head of list
    while (h != NULL){              //iterate through waiting list
        set_thread_state(h,THREAD_READY);   //set waiting threads to ready
        tlinsert(&ready_list, h);           //insert waiting threads to ready list
        h = h->list_next;                   //go to next node
    }  
    tlclear(list);                          //clear waiting list - they don't need to wait on the condition anymore
}

// INTERNAL FUNCTION DEFINITIONS
//

void init_main_thread(void) {
    // Note: _main_guard is at the base of the stack (where the stack pointer
    // starts), and _main_stack is the lowest address of the stack.
    main_thread.stack_size = (void*)_main_guard - (void*)_main_stack;
}

void init_idle_thread(void) {
    idle_thread.stack_size = (void*)_idle_guard - (void*)_idle_stack;
    
    _thread_setup (
        &idle_thread,
        idle_thread.stack_base,
        idle_thread_func, NULL);
    
    tlinsert(&ready_list, &idle_thread);
}

static void set_running_thread(struct thread * thr) {
    asm inline ("mv tp, %0" :: "r"(thr) : "tp");
}

const char * thread_state_name(enum thread_state state) {
    static const char * const names[] = {
        [THREAD_UNINITIALIZED] = "UNINITIALIZED",
        [THREAD_STOPPED] = "STOPPED",
        [THREAD_WAITING] = "WAITING",
        [THREAD_RUNNING] = "RUNNING",
        [THREAD_READY] = "READY",
        [THREAD_EXITED] = "EXITED"
    };

    if (0 <= (int)state && (int)state < sizeof(names)/sizeof(names[0]))
        return names[state];
    else
        return "UNDEFINED";
};

void recycle_thread(int tid) {
    struct thread * const thr = thrtab[tid];
    int ctid;

    assert (0 < tid && tid < NTHR && thr != NULL);
    assert (thr->state == THREAD_EXITED);

    // Make our parent the parent of our children

    for (ctid = 1; ctid < NTHR; ctid++) {
        if (thrtab[ctid] != NULL && thrtab[ctid]->parent == thr)
            thrtab[ctid]->parent = thr->parent;
    }

    thrtab[tid] = NULL;
    kfree(thr);
}

/*Suspends the execution of the current thread and switches to another ready thread
This function in thread.c suspends the current thread, removing it from execution and switching to the next
ready-to-run thread in the ready list. The suspend self function implements a simple round-robin scheduler.
The thread being suspended, if it is still runnable, is inserted at the tail of ready list, and the next thread to run
is taken from the head of the list. Note that suspend thread is called from thread yield, condition wait,
and thread exit. The calling thread may be RUNNING, WAITING, or EXITED. Only in the first case should
your implementation of suspend self place the current thread back on the ready-to-run list (and update its state
accordingly).*/

/*Function interface for suspend_self:

Suspend_self first verifies that the current thread is in fact running.
If it is running, it should be sent to the back of the ready list and its state should be set to ready
Then, to determine the next list to be ran, pop the front of the ready list (if it exists) and set 
it to running and switch to it. If it does not exist, run the idle thread instead.
*/
void suspend_self(void) {
    // FIXME your code here
    if(CURTHR != NULL && CURTHR->state == THREAD_RUNNING){ //check that it is running
        set_thread_state(CURTHR, THREAD_READY); //set running thread to ready
        tlinsert(&ready_list, CURTHR);          //insert running thread to back of ready list (round robin)
    }
    if(!tlempty(&ready_list)){                          //check there are threads ready to run
        struct thread * next = tlremove(&ready_list);   //grab next thread in line
        //trace("new thread: %s", next->name);
        set_thread_state(next, THREAD_RUNNING);         //set thread to running
        _thread_swtch(next);                            //call switch to next thread
    } else {                                            //if there are no ready threads, switch to idle thread
        set_thread_state(&idle_thread, THREAD_RUNNING); //set idle to running
        _thread_swtch(&idle_thread);                    //switch to idle
    }
}

void tlclear(struct thread_list * list) {
    list->head = NULL;
    list->tail = NULL;
}

int tlempty(const struct thread_list * list) {
    return (list->head == NULL);
}

void tlinsert(struct thread_list * list, struct thread * thr) {
    if (list->tail != NULL) {
        assert (list->head != NULL);
        list->tail->list_next = thr;
    } else {
        assert(list->head == NULL);
        list->head = thr;
    }

    list->tail = thr;
}

struct thread * tlremove(struct thread_list * list) {
    struct thread * const thr = list->head;

    assert(thr != NULL);
    list->head = thr->list_next;
    
    if (list->head != NULL)
        thr->list_next = NULL;
    else
        list->tail = NULL;
    
    return thr;
}

// Appends l1 to the end of l0. l1 remains unchanged, but is now part of l0.

void tlappend(struct thread_list * l0, const struct thread_list * l1) {
    if (l0->head != NULL) {
        assert(l0->tail != NULL);
        
        if (l1->head != NULL) {
            assert(l1->tail != NULL);
            l0->tail->list_next = l1->head;
            l0->tail = l1->tail;
        }
    } else {
        assert(l0->tail == NULL);
        l0->head = l1->head;
        l0->tail = l1->tail;
    }
}

void idle_thread_func(void * arg __attribute__ ((unused))) {
    // The idle thread sleeps using wfi if the ready list is empty. Note that we
    // need to disable interrupts before checking if the thread list is empty to
    // avoid a race condition where an ISR marks a thread ready to run between
    // the call to tlempty() and the wfi instruction.

    for (;;) {
        // If there are runnable threads, yield to them.

        while (!tlempty(&ready_list))
            thread_yield();
        
        // No runnable threads. Sleep using the wfi instruction. Note that we
        // need to disable interrupts and check the runnable thread list one
        // more time (make sure it is empty) to avoid a race condition where an
        // ISR marks a thread ready before we call the wfi instruction.

        intr_disable();
        if (tlempty(&ready_list))
            asm ("wfi");
        intr_enable();
    }
}