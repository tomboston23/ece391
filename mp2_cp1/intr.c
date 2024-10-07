// intr.c - Interrupt management
// 

#include "intr.h"
#include "trap.h"
#include "halt.h"
#include "csr.h"
#include "plic.h"

#include <stddef.h>

// INTERNAL COMPILE-TIME CONSTANT DEFINITIONS
//

#ifndef NIRQ
#define NIRQ 32
#endif

// EXPORTED GLOBAL VARIABLE DEFINITIONS
// 

int intr_initialized = 0;

// INTERNAL TYPE DEFINITIONS
// 

// ...

// INTERNAL GLOBAL VARIABLE DEFINITIONS
//

static struct {
    void (*isr)(int,void*);
    void * isr_aux;
    int prio;
} isrtab[NIRQ];

// INTERNAL FUNCTION DECLARATIONS
//

static void extern_intr_handler(void);

// EXPORTED FUNCTION DEFINITIONS
//

void intr_init(void) {
    intr_disable(); // should be disabled already
    plic_init();

    csrw_mip(0); // clear all pending interrupts
    csrw_mie(RISCV_MIE_MEIE); //enable interrupts from plic

    intr_initialized = 1;
}

void intr_register_isr (
    int irqno, int prio,
    void (*isr)(int irqno, void * aux),
    void * isr_aux)
{
    if (irqno < 0 || NIRQ <= irqno)
        panic("irqno out of bounds");

    if (prio <= 0)
        prio = 1;

    isrtab[irqno].isr = isr;
    isrtab[irqno].isr_aux = isr_aux;
    isrtab[irqno].prio = prio;
}


void intr_enable_irq(int irqno) {
    if (isrtab[irqno].isr == NULL)
        panic("intr_enable_irq with no isr");
    plic_enable_irq(irqno, isrtab[irqno].prio);
}

void intr_disable_irq(int irqno) {
    plic_disable_irq(irqno);
}

// INTERNAL FUNCTION DEFINITIONS
//

void intr_handler(int code) {
    switch (code) {
    case RISCV_MCAUSE_EXCODE_MEI:
        extern_intr_handler();
        break;
    default:
        panic("unhandled interrupt");
        break;
    }
}

void extern_intr_handler(void) {
    int irqno;

    irqno = plic_claim_irq();

    if (irqno < 0 || NIRQ <= irqno)
        panic("invalid irq");
    
    if (irqno == 0)
        return;
    
    if (isrtab[irqno].isr == NULL)
        panic("unhandled irq");
    
    // FIXME your code goes here
    isrtab[irqno].isr(irqno, isrtab[irqno].isr_aux); //call the function for our irqno using struct variables

    plic_close_irq(irqno);
}