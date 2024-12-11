// lock.h - A sleep lock
//

#ifdef LOCK_TRACE
#define TRACE
#endif

#ifdef LOCK_DEBUG
#define DEBUG
#endif

#ifndef _LOCK_H_
#define _LOCK_H_

#include "thread.h"
#include "halt.h"
#include "console.h"
#include "intr.h"

struct lock {
    struct condition cond;
    int tid; // thread holding lock or -1
};

static inline void lock_init(struct lock * lk, const char * name);
static inline void lock_acquire(struct lock * lk);
static inline void lock_release(struct lock * lk);

// INLINE FUNCTION DEFINITIONS
//

static inline void lock_init(struct lock * lk, const char * name) {
    trace("%s(<%s:%p>", __func__, name, lk);
    condition_init(&lk->cond, name);
    lk->tid = -1;
}

// Function: lock_acquire
// Description: Acquires a lock for the currently running thread. If the lock is already held
//              by another thread, the calling thread will wait until the lock becomes available.
// Parameters:
//   - lk: Pointer to the lock structure to be acquired.
static inline void lock_acquire(struct lock * lk) {
    // TODO: FIXME implement this
    intr_disable(); // disable interrupts
    while (lk->tid != -1) 
        condition_wait(&lk->cond); // wait until the lock condition is broadcasted
    lk->tid = running_thread(); // set the locked thread to running
    intr_enable(); // enable interrupts
}

static inline void lock_release(struct lock * lk) {
    trace("%s(<%s:%p>", __func__, lk->cond.name, lk);

    assert (lk->tid == running_thread());
    
    lk->tid = -1;
    condition_broadcast(&lk->cond);

    debug("Thread <%s:%d> released lock <%s:%p>",
        thread_name(running_thread()), running_thread(),
        lk->cond.name, lk);
}

#endif // _LOCK_H_