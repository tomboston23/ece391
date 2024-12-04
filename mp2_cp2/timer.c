// timer.c
//

#include "timer.h"
#include "thread.h"
#include "csr.h"
#include "intr.h"
#include "halt.h" // for assert

// EXPORTED GLOBAL VARIABLE DEFINITIONS
// 

char timer_initialized = 0;

struct condition tick_1Hz;
struct condition tick_10Hz;

uint64_t tick_1Hz_count;
uint64_t tick_10Hz_count;

#define MTIME_FREQ 10000000 // from QEMU include/hw/intc/riscv_aclint.h

// INTERNVAL GLOBAL VARIABLE DEFINITIONS
//

// INTERNAL FUNCTION DECLARATIONS
//

static inline uint64_t get_mtime(void);
static inline void set_mtime(uint64_t val);
static inline uint64_t get_mtimecmp(void);
static inline void set_mtimecmp(uint64_t val);

// EXPORTED FUNCTION DEFINITIONS
//

void timer_init(void) {
    assert (intr_initialized);
    condition_init(&tick_1Hz, "tick_1Hz");
    condition_init(&tick_10Hz, "tick_10Hz");

    // Set mtimecmp to maximum so timer interrupt does not fire

    set_mtime(0);
    set_mtimecmp(UINT64_MAX);
    csrc_mie(RISCV_MIE_MTIE);

    timer_initialized = 1;
}

void timer_start(void) {
    set_mtime(0);
    set_mtimecmp(MTIME_FREQ / 10);
    csrs_mie(RISCV_MIE_MTIE);
}

// timer_handle_interrupt() is dispatched from intr_handler in intr.c
/*This function in timer.c should signal the tick 10Hz condition 10 times per second and the tick 1Hz condition
once per second using condition broadcast. The global tick 10Hz count variable should count the number
of times tick 10Hz was signaled, and tick 1Hz count the number of times tick 10Hz was signaled. The timer
on the virt device increments 10 million times per second; use the constant MTIME FREQ in timer.c for this
value*/

/*Function interface for timer_intr_handler:

For this function, we need to get the time in ticks that has elapsed since the start of our code and 
compare it to our comparator cmp. When time >= cmp, we know 0.1 seconds have passed and we can increment
10Hz count and broadcast the tick_10Hz condition.
When 10 Hz count is a multiple of 10 we know a full second has passed so we can increment the 1 Hz count
and broadcast the tick_1Hz condition.
*/
void timer_intr_handler(void) {
    // FIXME your code goes here
    uint64_t time = get_mtime();        //get current tick cound
    uint64_t cmp = get_mtimecmp();      //get comparator
    //cmp represents the threshold the time needs to cross in order to count
    if (cmp == UINT64_MAX){             //make sure cmp is initialized
        set_mtimecmp(MTIME_FREQ/10);    //if not, set to initialized value
    }
    if(time >= cmp){ //see if 10Hz threshold was crossed
        tick_10Hz_count++;  //increment 10Hz counter
        //trace("tick_10Hz_count = %d", tick_10Hz_count);
        condition_broadcast(&tick_10Hz); //broadcast 10Hz
        if (tick_10Hz_count%10 == 0){ //see if 1Hz threshold was crossed
            tick_1Hz_count++;         //increment 1Hz counter
            condition_broadcast(&tick_1Hz);//broadcast 1Hz
            //trace("tick_1Hz_count = %d", tick_1Hz_count); 
        }
        cmp += MTIME_FREQ/10;       //cmp should now be 0.1 seconds later than it was before
        set_mtimecmp(cmp); //set next cmp time to 1/10 seconds ahead
    }

}

// Hard-coded MTIMER device addresses for QEMU virt device

#define MTIME_ADDR 0x200BFF8
#define MTCMP_ADDR 0x2004000

static inline uint64_t get_mtime(void) {
    return *(volatile uint64_t*)MTIME_ADDR;
}

static inline void set_mtime(uint64_t val) {
    *(volatile uint64_t*)MTIME_ADDR = val;
}

static inline uint64_t get_mtimecmp(void) {
    return *(volatile uint64_t*)MTCMP_ADDR;
}

static inline void set_mtimecmp(uint64_t val) {
    *(volatile uint64_t*)MTCMP_ADDR = val;
}
