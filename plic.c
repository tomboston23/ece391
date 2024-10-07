// plic.c - RISC-V PLIC
//

#include "plic.h"
#include "console.h"

#include <stdint.h>

// COMPILE-TIME CONFIGURATION
//

// *** Note to student: you MUST use PLIC_IOBASE for all address calculations,
// as this will be used for testing!

#ifndef PLIC_IOBASE
#define PLIC_IOBASE 0x0C000000
#endif

#define PLIC_SRCCNT 0x400
#define PLIC_CTXCNT 1

uint64_t pending = PLIC_IOBASE + 0x1000; //0x1000 is the pending offset for src 0-32
uint64_t enable = PLIC_IOBASE + 0x2000; //0x2000 is the enable offset for src 0-32
uint64_t threshold = PLIC_IOBASE + 0x200000; //0x200000 is the threshold for ctx 0
uint64_t claim = PLIC_IOBASE + 0x200004; //0x200004 is the claim offset for ctx 0


// INTERNAL FUNCTION DECLARATIONS
//

// *** Note to student: the following MUST be declared extern. Do not change these
// function delcarations!

extern void plic_set_source_priority(uint32_t srcno, uint32_t level);
extern int plic_source_pending(uint32_t srcno);
extern void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_set_context_threshold(uint32_t ctxno, uint32_t level);
extern uint32_t plic_claim_context_interrupt(uint32_t ctxno);
extern void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno);

// Currently supports only single-hart operation. The low-level PLIC functions
// already understand contexts, so we only need to modify the high-level
// functions (plic_init, plic_claim, plic_complete).

// EXPORTED FUNCTION DEFINITIONS
// 

void plic_init(void) {
    int i;

    // Disable all sources by setting priority to 0, enable all sources for
    // context 0 (M mode on hart 0).

    for (i = 0; i < PLIC_SRCCNT; i++) {
        plic_set_source_priority(i, 0);
        plic_enable_source_for_context(0, i);
    }
}

extern void plic_enable_irq(int irqno, int prio) {
    trace("%s(irqno=%d,prio=%d)", __func__, irqno, prio);
    plic_set_source_priority(irqno, prio);
}

extern void plic_disable_irq(int irqno) {
    if (0 < irqno)
        plic_set_source_priority(irqno, 0);
    else
        debug("plic_disable_irq called with irqno = %d", irqno);
}

extern int plic_claim_irq(void) {
    // Hardwired context 0 (M mode on hart 0)
    trace("%s()", __func__);
    return plic_claim_context_interrupt(0);
}

extern void plic_close_irq(int irqno) {
    // Hardwired context 0 (M mode on hart 0)
    trace("%s(irqno=%d)", __func__, irqno);
    plic_complete_context_interrupt(0, irqno);
}

// INTERNAL FUNCTION DEFINITIONS
//

void plic_set_source_priority(uint32_t srcno, uint32_t level) {
    // FIXME your code goes here
    if(srcno > PLIC_SRCCNT || srcno == 0 || level < PLIC_PRIO_MIN){ // return if invalid inputs
        return;
    if(level > PLIC_PRIO_MAX) {level = PLIC_PRIO_MAX;}//deal with case where priority is too high
    } else {
        volatile uint32_t * prio_ptr = (volatile uint32_t*)((uint64_t)PLIC_IOBASE + sizeof(uint32_t)*srcno); //find ptr to priority of our scrno
        //4 bytes (sizeof uint32_t) are allocated for each priority. this is why I multiplied srcno*4 to get offset
        //for example, priority of source 2 is at IOBASE + 8

        (*prio_ptr) = level; //set priority of our srcno to level
    }
}

int plic_source_pending(uint32_t srcno) {
    // FIXME your code goes here
    if(srcno > PLIC_SRCCNT){return 0;} //return if invalid
    volatile uint32_t * pending_ptr = (volatile uint32_t*)(pending + 4*(srcno/32)); //find pointer containing our pending bit
    //32 here represents 32 bits per 4-byte address pointed to by our enable_ptr
    //we divide by 32 to find how many times we need to increment our pointer
    //multiply by size of uint32 to convert our offset to the address for our specific source

    uint32_t pend = *pending_ptr; //get pending int that contains pending data for our source
    pend &= (1 << (srcno%32)); //look at pending bit of our srcno and return
    //we use %32 because there are 32 bits pointed to by our enable_ptr
    //we read bit srcno % 32 because that bit represents our srcno

    return (int)(pend == 1); //return 1 if pending = 1, 0 else
}

void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno) {
    // FIXME your code goes here
    if(ctxno != 0 || srcno > PLIC_SRCCNT || srcno <= 0){ //return if invalid inputs
        return;
    }
    volatile uint32_t * enable_ptr = (volatile uint32_t*)(enable + sizeof(uint32_t)*(srcno/32)); //find ptr containing our enable bit
    //32 here represents 32 bits per 4-byte address pointed to by our enable_ptr
    //we divide by 32 to find how many times we need to increment our pointer
    //multiply by size of uint32 to convert our offset to the actual correct address

    (*enable_ptr) |= (1 << (srcno%32)); //set bit of our srcno to 1 and return
    //we use %32 because there are 32 bits pointed to by our enable_ptr
    //we set bit srcno % 32 to 1 because that bit represents our srcno
}

void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcid) {
    // FIXME your code goes here
    if(ctxno != 0 || srcid > PLIC_SRCCNT || srcid <= 0){ //return if invalid inputs
        return;
    }
    volatile uint32_t * enable_ptr = (volatile uint32_t*)(enable + 4*(srcid/32)); //find ptr to our enable bit
    //32 here represents 32 bits per 4-byte address pointed to by our enable_ptr
    //we divide by 32 to find how many times we need to increment our pointer
    //multiply by size of uint32 to convert our offset to the actual correct address

    (*enable_ptr) &= ~(1 << (srcid%32)); //set bit of our srcno to 0 and return
    //we use %32 because there are 32 bits pointed to by our enable_ptr
    //we set bit srcno % 32 to 0 because that bit represents our srcno
}

void plic_set_context_threshold(uint32_t ctxno, uint32_t level) {
    // FIXME your code goes here
    if(ctxno != 0){ //exit if invalid context
        return;
    }
    volatile uint32_t * threshold_ptr = (volatile uint32_t*)(threshold); //find our context's threshold (we know ctxno = 0)
    (*threshold_ptr) = level; //set context 0's threshold to level
}

uint32_t plic_claim_context_interrupt(uint32_t ctxno) {
    // FIXME your code goes here
    if(ctxno != 0){ //exit if invalid ctxno
        return 0;
    }
    volatile uint32_t * claim_ptr = (volatile uint32_t*)(claim); //find context 0's claim ptr
    return *claim_ptr; //return the claim register
}

void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno) {
    // FIXME your code goes here
    if(ctxno != 0 || srcno > PLIC_SRCCNT || srcno <= 0){ // exit if invalid ctxno or srcno
        return;
    }
    volatile uint32_t * claim_ptr = (volatile uint32_t*)(claim); //find claim ptr for context 0
    (*claim_ptr) = srcno; //set claim register to srcno
}