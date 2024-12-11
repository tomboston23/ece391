// memory.c - Memory management
//

#ifndef TRACE
#ifdef MEMORY_TRACE
#define TRACE
#endif
#endif

#ifndef DEBUG
#ifdef MEMORY_DEBUG
#define DEBUG
#endif
#endif

#include "config.h"

#include "memory.h"
#include "console.h"
#include "halt.h"
#include "heap.h"
#include "csr.h"
#include "string.h"
#include "error.h"
#include "thread.h"
#include "process.h"

#include <stdint.h>

#define TOT_LEVELS 2

// EXPORTED VARIABLE DEFINITIONS
//

char memory_initialized = 0;
uintptr_t main_mtag;

// IMPORTED VARIABLE DECLARATIONS
//

// The following are provided by the linker (kernel.ld)

extern char _kimg_start[];
extern char _kimg_text_start[];
extern char _kimg_text_end[];
extern char _kimg_rodata_start[];
extern char _kimg_rodata_end[];
extern char _kimg_data_start[];
extern char _kimg_data_end[];
extern char _kimg_end[];

// INTERNAL TYPE DEFINITIONS
//

union linked_page {
    union linked_page * next;
    char padding[PAGE_SIZE];
}; //a page of memory that we store in the linked lists

struct pte { //64 bit number with all the page table entry data and shi
    uint64_t flags:8; //these are like another way to do a bitshift 
    uint64_t rsw:2;
    uint64_t ppn:44;
    uint64_t reserved:7;
    uint64_t pbmt:2;
    uint64_t n:1;
};

// INTERNAL MACRO DEFINITIONS
//

#define VPN2(vma) (((vma) >> (9+9+12)) & 0x1FF)
#define VPN1(vma) (((vma) >> (9+12)) & 0x1FF)
#define VPN0(vma) (((vma) >> 12) & 0x1FF)
#define MIN(a,b) (((a)<(b))?(a):(b))

// INTERNAL FUNCTION DECLARATIONS
//

static inline int wellformed_vma(uintptr_t vma);
static inline int wellformed_vptr(const void * vp);
static inline int aligned_addr(uintptr_t vma, size_t blksz);
static inline int aligned_ptr(const void * p, size_t blksz);
static inline int aligned_size(size_t size, size_t blksz);

static inline uintptr_t active_space_mtag(void);
static inline struct pte * mtag_to_root(uintptr_t mtag);
static inline struct pte * active_space_root(void);

static inline void * pagenum_to_pageptr(uintptr_t n);
static inline uintptr_t pageptr_to_pagenum(const void * p);

static inline void * round_up_ptr(void * p, size_t blksz);
static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz);
static inline size_t round_up_size(size_t n, size_t blksz);
static inline void * round_down_ptr(void * p, size_t blksz);
static inline size_t round_down_size(size_t n, size_t blksz);
static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz);

static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags);
static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag);
static inline struct pte null_pte(void);

static inline void sfence_vma(void);

// INTERNAL GLOBAL VARIABLES
//

static union linked_page * free_list;

static struct pte main_pt2[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt1_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt0_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));

// EXPORTED VARIABLE DEFINITIONS
//

// EXPORTED FUNCTION DEFINITIONS
// 
/*void memory_init
// Sets up page tables and performs virtual-to-physical 1:1 mapping of the kernel megapage. Enables
Sv39 paging. Initializes the heap memory manager. Puts free pages on the free pages list. Allows S
mode access of U mode memory. We have provided most of this function for you.
// @param void: needs no parameters
// @returns void: returns nothing*/
void memory_init(void) {
    const void * const text_start = _kimg_text_start;
    kprintf("\n\n_kimg_text_start:%p, _kimg_text_end: %p\n", _kimg_text_start, _kimg_data_end);
    const void * const text_end = _kimg_text_end;
    const void * const rodata_start = _kimg_rodata_start;
    const void * const rodata_end = _kimg_rodata_end;
    const void * const data_start = _kimg_data_start;
    //union linked_page * page;
    void * heap_start;
    void * heap_end;
    size_t page_cnt;
    uintptr_t pma;
    const void * pp;

    trace("%s()", __func__);

    assert (RAM_START == _kimg_start);

    kprintf("           RAM: [%p,%p): %zu MB\n",
        RAM_START, RAM_END, RAM_SIZE / 1024 / 1024);
    kprintf("  Kernel image: [%p,%p)\n", _kimg_start, _kimg_end);

    // Kernel must fit inside 2MB megapage (one level 1 PTE)
    
    if (MEGA_SIZE < _kimg_end - _kimg_start)
        panic("Kernel too large");

    // Initialize main page table with the following direct mapping:
    // 
    //         0 to RAM_START:           RW gigapages (MMIO region)
    // RAM_START to _kimg_end:           RX/R/RW pages based on kernel image
    // _kimg_end to RAM_START+MEGA_SIZE: RW pages (heap and free page pool)
    // RAM_START+MEGA_SIZE to RAM_END:   RW megapages (free page pool)
    //
    // RAM_START = 0x80000000
    // MEGA_SIZE = 2 MB
    // GIGA_SIZE = 1 GB
    
    // Identity mapping of two gigabytes (as two gigapage mappings)
    for (pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
        main_pt2[VPN2(pma)] = leaf_pte((void*)pma, PTE_R | PTE_W | PTE_G);
    
    // Third gigarange has a second-level page table
    main_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);

    // First physical megarange of RAM is mapped as individual pages with
    // permissions based on kernel image region.

    main_pt1_0x80000[VPN1(RAM_START_PMA)] =
        ptab_pte(main_pt0_0x80000, PTE_G);

    for (pp = text_start; pp < text_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_X | PTE_G);
    }

    for (pp = rodata_start; pp < rodata_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = data_start; pp < RAM_START + MEGA_SIZE; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Remaining RAM mapped in 2MB megapages

    for (pp = RAM_START + MEGA_SIZE; pp < RAM_END; pp += MEGA_SIZE) {
        main_pt1_0x80000[VPN1((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Enable paging. This part always makes me nervous.

    main_mtag =  // Sv39
        ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
        pageptr_to_pagenum(main_pt2);
    
    csrw_satp(main_mtag);
    sfence_vma();

    // Give the memory between the end of the kernel image and the next page
    // boundary to the heap allocator, but make sure it is at least
    // HEAP_INIT_MIN bytes.

    heap_start = _kimg_end;
    heap_end = round_up_ptr(heap_start, PAGE_SIZE);
    if (heap_end - heap_start < HEAP_INIT_MIN) {
        heap_end += round_up_size (
            HEAP_INIT_MIN - (heap_end - heap_start), PAGE_SIZE);
    }

    if (RAM_END < heap_end)
        panic("Not enough memory");
    
    // Initialize heap memory manager

    heap_init(heap_start, heap_end);

    kprintf("\nHeap allocator: [%p,%p): %zu KB free\n",
        heap_start, heap_end, (heap_end - heap_start) / 1024);

    free_list = heap_end; // heap_end is page aligned
    page_cnt = (RAM_END - heap_end) / PAGE_SIZE;

    kprintf("Page allocator: [%p,%p): %lu pages free\n",
        free_list, RAM_END, page_cnt);

    // Put free pages on the free page list
    // TODO: FIXME implement this (must work with your implementation of
    // memory_alloc_page and memory_free_page).
    for(pp = free_list; pp < (void*)RAM_END; pp+=PAGE_SIZE){
        memory_free_page((void*)pp); //add all the available physical memory into free list
    }
    
    // Allow supervisor to access user memory. We could be more precise by only
    // enabling it when we are accessing user memory, and disable it at other
    // times to catch bugs.

    csrs_sstatus(RISCV_SSTATUS_SUM);

    memory_initialized = 1;
}

// INTERNAL FUNCTION DEFINITIONS
//

static inline int wellformed_vma(uintptr_t vma) {
    // Address bits 63:38 must be all 0 or all 1
    uintptr_t const bits = (intptr_t)vma >> 38;
    return (!bits || !(bits+1));
}

static inline int wellformed_vptr(const void * vp) {
    return wellformed_vma((uintptr_t)vp);
}

static inline int aligned_addr(uintptr_t vma, size_t blksz) {
    return ((vma % blksz) == 0);
}

static inline int aligned_ptr(const void * p, size_t blksz) {
    return (aligned_addr((uintptr_t)p, blksz));
}

static inline int aligned_size(size_t size, size_t blksz) {
    return ((size % blksz) == 0);
}

static inline uintptr_t active_space_mtag(void) {
    return csrr_satp();
}

static inline struct pte * mtag_to_root(uintptr_t mtag) {
    return (struct pte *)((mtag << 20) >> 8);
}


static inline struct pte * active_space_root(void) {
    return mtag_to_root(active_space_mtag());
}

static inline void * pagenum_to_pageptr(uintptr_t n) {
    return (void*)(n << PAGE_ORDER);
}

static inline uintptr_t pageptr_to_pagenum(const void * p) {
    return (uintptr_t)p >> PAGE_ORDER;
}

static inline void * round_up_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)(p + blksz-1) / blksz * blksz);
}

static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz) {
    return ((addr + blksz-1) / blksz * blksz);
}

static inline size_t round_up_size(size_t n, size_t blksz) {
    return (n + blksz-1) / blksz * blksz;
}

static inline void * round_down_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)p / blksz * blksz);
}

static inline size_t round_down_size(size_t n, size_t blksz) {
    return n / blksz * blksz;
}

static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz) {
    return (addr / blksz * blksz);
}

static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags)
{
    //kprintf("lfp...");
    return (struct pte) {
        .flags = rwxug_flags | PTE_A | PTE_D | PTE_V,
        .ppn = pageptr_to_pagenum(pptr)
    };
}

static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag)
{
    return (struct pte) {
        .flags = g_flag | PTE_V,
        .ppn = pageptr_to_pagenum(ptab)
    };
}

static inline struct pte null_pte(void) {
    return (struct pte) { };
}

static inline void sfence_vma(void) {
    asm inline ("sfence.vma" ::: "memory");
}

// uintptr_t memory_space_create(void)
// Creates a new memory space and makes it the currently active space. Returns a
// memory space tag (type uintptr_t) that may be used to refer to the memory
// space. The created memory space contains the same identity mapping of MMIO
// address space and RAM as the main memory space. This function never fails; if
// there are not enough physical memory pages to create the new memory space, it
// panics.
//@params: uint_fast16_t asid: address space id used to identify a virtual memory space
//@returns: uintptr_t satrKey: it returns a pointer to the memory space tag that we have just switched to!!

uintptr_t memory_space_create(uint_fast16_t asid){
    struct pte* petah = (struct pte*)memory_alloc_page(); //get some memory from the free list
    memset(petah, 0, PAGE_SIZE); //clean the memory
    for(size_t i = 0; i< PTE_CNT; i++){
        petah[i] = main_pt2[i]; //copy kernel diagnostics
    }
    //we need to write into satp which has the following format: Base = SV address type(in our case its SV39), ASID = the fookin thing that was passed in, Physical Page number (PPN) of physical memory address 
    uintptr_t satrKey = ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift    |    (uintptr_t)asid << RISCV_SATP_ASID_shift     |    pageptr_to_pagenum(petah));
    memory_space_switch(satrKey); //switch to the next memory space
    return satrKey;
};

// void memory_space_reclaim(void)
// Switches the active memory space to the main memory space and reclaims the
// memory space that was active on entry. All physical pages mapped by a user
// mapping are reclaimed. We free all pages from the previous memory space i guess.
//@param: none
//@return: none
void memory_space_reclaim(void) {
    uintptr_t old_mtag = active_space_mtag();
    if (old_mtag == main_mtag) {
        //kprintf("Already in main memory space; nothing to reclaim.\n");
        return; //no need to do anything if already in main memory space
    }

    memory_unmap_and_free_user(); // Reclaim all user-space mappings
    memory_free_page(mtag_to_root(old_mtag)); // Free root page table if not main

    // Switch back to main memory space
    csrw_satp(main_mtag);
    sfence_vma(); //flush the TLB
}


// void * memory_alloc_page(void)
// Allocates a physical page of memory. Returns a pointer to the direct-mapped
// address of the page. Does not fail; panics if there are no free pages available.

void * memory_alloc_page(void){
    if(free_list == NULL){
        panic("No free pages available");
        //process_exit();
    }
    union linked_page *addr = free_list; //literally just return the pointer at the top of the free list
    if(free_list->next != NULL){
        free_list = free_list->next; //and then obviously just move the head
    }
    return (void*) addr;
}

// void memory_free_page(void * ptr)
// Returns a physical memory page to the physical page allocator. The page must
// have been previously allocated by memory_alloc_page.

void memory_free_page(void * pp){
    void *heap_start = _kimg_end; //get the heap start location
    void* heap_end = round_up_ptr(heap_start, PAGE_SIZE); //get the end of the heap
    if(!(pp >= heap_end && pp < (void*)RAM_END)){
        panic("Not a previously allocated pointer from memory_alloc_page");
    }
    union linked_page* ppLink= (union linked_page*) pp; //type cast to union linked_page
    ppLink->next = free_list; //push the new page to the front of the free list (doesnt have to be front but just has to be in the list i guess)
    free_list = ppLink; //update the head
}



//walk_pt: helper function to walk through vpn page tables to returns the pte pointer to the level 0 entry corresponding to vma
//@param: struct pte* root: the root table to actually start in
//@param: uintptr_t vma: virtual memory address
//@param: int create: if this is high, then we create page tables if they don't exist
struct pte *walk_pt(struct pte *root, uintptr_t vma, int create) {
    if (!root || !wellformed_vma(vma)) {
        return NULL; // Invalid input if root is null or the vma is not valid
    }

    for (int level = TOT_LEVELS; level > 0; level--) { //loop through the memory tables
        size_t idx = (level == TOT_LEVELS) ? VPN2(vma) : VPN1(vma);
        struct pte *pte = &root[idx];

        if (!(pte->flags & PTE_V)) {
            if (!create) {
                return NULL; // If not creating, return NULL
            }
            // Allocate and initialize a new page table
            struct pte *new_pt = (struct pte *)memory_alloc_page();
            if (!new_pt) {
                panic("walk_pt: Out of memory during table creation");
            }
            memset(new_pt, 0, PAGE_SIZE);
            *pte = ptab_pte(new_pt, PTE_V);
        }

        // Move to the next level
        root = (struct pte *)pagenum_to_pageptr(pte->ppn);
    }

    // Return the level 0 entry
    return &root[VPN0(vma)];
}


// void * memory_alloc_and_map_page (
//        uintptr_t vma, uint_fast8_t rwxug_flags)
// Allocates and maps a physical page.
// Maps a virtual page to a physical page in the current memory space. The /vma/
// argument gives the virtual address of the page to map. The /pp/ argument is a
// pointer to the physical page to map. The /rwxug_flags/ argument is an OR of
// the PTE flags, of which only a combination of R, W, X, U, and G should be
// specified. (The D, A, and V flags are always added by memory_map_page.) The
// function returns a pointer to the mapped virtual page, i.e., (void*)vma.
// Does not fail; panics if the request cannot be satsified.
void * memory_alloc_and_map_page (uintptr_t vma, uint_fast8_t rwxug_flags){
    //kprintf("memory_alloc start\n");
    if(!wellformed_vma(vma) || !aligned_addr(vma, PAGE_SIZE)){
        panic("Not a valid vma inputted"); //we have to check if the vma is SV39 aligned and that the address is aligned by 4kb
    }
    void* peepee = memory_alloc_page(); //get a physical page
    //kprintf("successfully allocated a page\n");
    struct pte* petah= walk_pt(mtag_to_root(active_memory_space()), vma, 1); //get the page table entry associated with a vma at the last table (this includes the ppn (but not offset thats in vma) of the vma)
    //petah (right now in this line of code) is important since it is at the address we want it to be in the level0 subtable!
    //kprintf("walk pte success: petah: %p\n", petah);
    if (!peepee || !aligned_ptr(peepee, PAGE_SIZE)) {
        panic("Failed to allocate a valid physical page");
    }
    if (!petah) {
        panic("Failed to walk page table for VMA\n");
    }
    // if(petah->flags & PTE_V)
    //     panic("not a valid pte type shi");
    struct pte jit = leaf_pte((const void*)peepee, rwxug_flags);  //converts actual physical memory address to a physical page number and adds to the specified flags to a page table entry

    //kprintf("\n\nPETAHH: address: %p\n", petah);
    //kprintf("jit's flags: %x\n", jit.flags);
    //kprintf("jit's ppn: %x\n", jit.ppn);
    petah->flags |= jit.flags; //add the correct flags
    petah->ppn |= jit.ppn; //add the correct ppn
    //kprintf("Mapped VMA %p to physical page %p with flags %x\n", (void*)vma, peepee, jit.flags);
    sfence_vma();
    return (void*) vma;

}
// void * memory_alloc_and_map_range (
//        uintptr_t vma, size_t size, uint_fast8_t rwxug_flags)size

// Allocates and maps multiple physical pages in an address range. Equivalent to
// calling memory_alloc_and_map_page for every page in the range.

void * memory_alloc_and_map_range (uintptr_t vma, size_t size, uint_fast8_t rwxug_flags){
    if(!wellformed_vma(vma) || !aligned_addr(vma, PAGE_SIZE)) panic("virtual address is either not well formed or not aligned");
    if(size == 0 || (size%PAGE_SIZE != 0)) size = round_up_size(size, PAGE_SIZE); //just round up the page size worst case type shi
    //^^ these are just some checks to see if vma is proper
    size_t numberOfPages = size/PAGE_SIZE; //get the number of pages
    for(size_t i =0; i<numberOfPages; i++){
        uintptr_t currVMA = vma + (i * PAGE_SIZE); //get the current vma
        memory_alloc_and_map_page(currVMA, rwxug_flags); //alloc space
    }

    memory_set_range_flags((const void *)vma, size, rwxug_flags); //set the flags and shi
    return (void*) vma;
}



// void memory_unmap_and_free_range(void * vp, size_t size)

// void memory_unmap_and_free_user(void)
// Unmaps and frees all pages with the U bit set in the PTE flags.

void memory_unmap_and_free_user(void) {
    uintptr_t vma;
    struct pte *pt2, *pt1, *pt0;
    void *pp;

    trace("%s()", __func__);

    if (!memory_initialized) {
        panic("Memory not initialized");
    }

    pt2 = active_space_root(); // Get the root page table
    if (!pt2) {
        panic("Failed to retrieve active space root");
    }

    for (vma = USER_START_VMA; vma < USER_END_VMA; vma += GIGA_SIZE) { // Iterate over the user memory space
        if (!(pt2[VPN2(vma)].flags & PTE_V)) { 
            // Skip non-valid entries at the top level
            continue;
        }

        // Translate the level 1 page table
        pt1 = (struct pte *)pagenum_to_pageptr(pt2[VPN2(vma)].ppn);
        if (!wellformed_vptr(pt1)) {
            panic("Invalid level 1 page table pointer");
        }

        for (size_t i = 0; i < PTE_CNT; i++) { // Iterate over level 1 table
            if (!(pt1[i].flags & PTE_V)) { 
                // Skip non-valid entries at the second level
                continue;
            }

            // Translate the level 0 page table
            pt0 = (struct pte *)pagenum_to_pageptr(pt1[i].ppn);
            if (!wellformed_vptr(pt0)) {
                panic("Invalid level 0 page table pointer");
            }

            for (size_t j = 0; j < PTE_CNT; j++) { // Iterate over level 0 table
                if (pt0[j].flags & PTE_V) { 
                    // Check if the entry is valid and has user permissions
                    if (pt0[j].flags & PTE_U) { 
                        // Free the physical page
                        pp = pagenum_to_pageptr(pt0[j].ppn);
                        if (!wellformed_vptr(pp)) {
                            panic("Invalid physical page pointer");
                        }
                        memory_free_page(pp);
                    }
                    // Clear the valid flag for the PTE
                    pt0[j].flags &= ~PTE_V;
                }
            }
            // Free the level 0 page table itself
            memory_free_page(pt0);
            pt1[i].flags &= ~PTE_V; // Clear the valid flag for the level 1 entry
        }
        // Free the level 1 page table itself
        memory_free_page(pt1);
        pt2[VPN2(vma)].flags &= ~PTE_V; // Clear the valid flag for the level 2 entry
    }



    // Flush the TLB to remove stale entries
    sfence_vma();
}


/*questions to ask:
1) whats vp in memory_set_range_flags
2) are we freeing a certain memroy space for memory_unmap_and_free_user??
3) do we need to enable the U flag in memory_alloc?


*/

// void memory_set_range_flags (
//      const void * vp, size_t size, uint_fast8_t rwxug_flags)
// Chnages the PTE flags for all pages in a mapped range.
//@param: const void* vp: starting virtual address of the range; size_t size = size of area we to map; rwxug_flags: the flags we need to implement
//@return: returns nothing
void memory_set_range_flags(const void *vp, size_t size, uint_fast8_t rwxug_flags) {
    // Validate that the base virtual address is well-formed and aligned
    if (!aligned_ptr(vp, PAGE_SIZE)) {
        kprintf("Error: Unaligned virtual address %p in memory_set_range_flags\n", vp);
        return;
    }

    if (size == 0 || size % PAGE_SIZE != 0) {
        size = round_up_size(size, PAGE_SIZE); // Align size to the next page boundary
    }

    uintptr_t start_vma = (uintptr_t)vp; //get the start address of the virtual pointer address
    uintptr_t end_vma = start_vma + size; //get the end of the virtual pointer address

    for (uintptr_t vma = start_vma; vma < end_vma; vma += PAGE_SIZE) { //loop through the range of address
        struct pte *pte = walk_pt(active_space_root(), vma, 0); //get the level 2 page table entry of the current vma being looped through
        if (!pte) { //if we were unable to get the correct pte, just skip that vma
            kprintf("Warning: No valid PTE for VMA %lx, skipping\n", vma);
            continue;
        }

        // Update the flags in the page table entry
        pte->flags = (pte->flags & ~(PTE_R | PTE_W | PTE_X | PTE_U | PTE_G)) | rwxug_flags | PTE_V | PTE_A | PTE_D; //add the correct flags (clear first then add the rwxug flags)
        //kprintf("Updated PTE for VMA %p with flags %x\n", (void *)vma, pte->flags);
    }

    // Flush the TLB after modifying all relevant PTEs
    sfence_vma();
    //kprintf("Completed flag update for range [%p, %p) with flags %x\n", vp, (void *)end_vma, rwxug_flags);
}



// int memory_validate_vptr_len (
//     const void * vp, size_t len, uint_fast8_t rwxug_flags);
// Checks if a virtual address range is mapped with specified flags. Returns 1
// if and only if every virtual page containing the specified virtual address
// range is mapped with the at least the specified flags.

// int memory_validate_vstr (
//     const char * vs, uint_fast8_t ug_flags)
// Checks if the virtual pointer points to a mapped range containing a
// null-terminated string. Returns 1 if and only if the virtual pointer points
// to a mapped readable page with the specified flags, and every byte starting
// at /vs/ up until the terminating null byte is also mapped with the same
// permissions.

int memory_validate_vstr (const char * vs, uint_fast8_t ug_flags);

// Called from excp.c to handle a page fault at the specified address. Either
// maps a page containing the faulting address, or calls process_exit().
// Handle a page fault at a virtual address. May choose to panic or to allocate a new page, depending on
// if the address is within the user region. You must call this function when a store page fault is triggered
// by a user program
int inRange(const void* vptr){ 
    if(vptr >= (void*)USER_START_VMA && vptr < (void*)USER_END_VMA){ //helper function to check if the virtual address is in the specified range from config.h
        return 1;
    }
    return 0;
}


// void memory_handle_page_fault (
//      const void *vptr)
// Handles store, load, and instruction page faults at unallocated pages (lazy allocation)
//@param: const void* vptr: starting virtual address of the fault
//@return: returns nothing
void memory_handle_page_fault(const void *vptr) {
    uintptr_t vma = round_down_addr((uintptr_t)vptr, PAGE_SIZE); //round down the virtual memory address

    if (!wellformed_vptr(vptr) || !inRange(vptr)) { //make sure the vptr is valid and is in the correct range
        kprintf("Page fault handler received an invalid address: %p\n", vptr);
        panic("Invalid page fault address");
    }

    kprintf("Handling page fault for vaddr: %p\n", vptr);

    struct pte *pte = walk_pt(active_space_root(), vma, 1); //get the associated level 2 pte of the associated vma 
    if (pte->flags & PTE_V) { //if the pte has a valid flag checked then we don't need to alloc anything
        kprintf("PTE already valid for VMA %p (flags=%x)\n", (void *)vma, pte->flags);
        panic("page already mapped");
    } else {
        void *page = memory_alloc_page(); //create the correct associated page
        *pte = leaf_pte(page, PTE_R | PTE_W | PTE_U | PTE_A | PTE_D | PTE_V); //format the pte to be a leaf pte 
        kprintf("Mapped new page for VMA %p to physical page %p\n", (void *)vma, page);
    }

    sfence_vma(); // Flush TLB
}


// Function: memory_space_clone
// Description: Clones the memory space of the current process, creating a new memory space
//              for the child process, including a deep copy of all user-space mappings.
// Parameters:
//   - asid: Address Space Identifier (ASID) for the new memory space (typically 0).
// Returns:
//   - The new memory tag (SATP value) for the cloned memory space.
//   - This function panics if memory allocation or copying fails.
uintptr_t memory_space_clone(uint_fast16_t asid) {
    struct pte *parent_root = active_space_root(); // get parent pte root
    if (!parent_root) {
        panic("Failed to retrieve parent process page table root");
    }

    struct pte *child_root = (struct pte *)memory_alloc_page(); // allocate memory for the child root (Level 2 page table)
    if (!child_root) {
        panic("Failed to allocate memory for child root page table");
    }
    memset(child_root, 0, PAGE_SIZE);

    for (size_t vpn2 = 0; vpn2 < 3; vpn2++) {
        child_root[vpn2] = parent_root[vpn2]; // shallow copy the first three entries (kernel and MMIO mappings)
    }

    for (uintptr_t vma = USER_START_VMA; vma < USER_END_VMA; vma += GIGA_SIZE) {
        size_t vpn2_idx = VPN2(vma); // find level 2 vpn index

        struct pte *parent_pt1 = (struct pte *)pagenum_to_pageptr(parent_root[vpn2_idx].ppn); // get parent page table at level 1
        if (!(parent_root[vpn2_idx].flags & PTE_V) || !parent_pt1) {
            continue; // skip if the pt2 values aren't valid, or if the pt1 pointer doesn't exist
        }

        // Allocate a new Level 1 page table for the child
        struct pte *child_pt1 = (struct pte *)memory_alloc_page(); // allocate a new page for child
        if (!child_pt1) {
            panic("Failed to allocate memory for child PT1");
        }
        memset(child_pt1, 0, PAGE_SIZE); // safe set to 0
        child_root[vpn2_idx] = ptab_pte(child_pt1, parent_root[vpn2_idx].flags); // set all the flags for the child pt2

        for (size_t vpn1 = 0; vpn1 < PTE_CNT; vpn1++) {
            struct pte *parent_pt0 = (struct pte *)pagenum_to_pageptr(parent_pt1[vpn1].ppn); // run it back with pt1 and pt0
            if (!(parent_pt1[vpn1].flags & PTE_V) || !parent_pt0) {
                continue; // skip if the pt1 values aren't valid, or if the pt0 pointer doesn't exist
            }

            // Allocate a new Level 0 page table for the child
            struct pte *child_pt0 = (struct pte *)memory_alloc_page(); // allocate a leaf page
            if (!child_pt0) {
                panic("Failed to allocate memory for child PT0");
            }
            memset(child_pt0, 0, PAGE_SIZE);
            child_pt1[vpn1] = ptab_pte(child_pt0, parent_pt1[vpn1].flags); // set flags for the child pt2


            for (size_t vpn0 = 0; vpn0 < PTE_CNT; vpn0++) {
                if (!(parent_pt0[vpn0].flags & PTE_V)) {
                    continue; // skip if pt0 values aren't valid
                }

                if (parent_pt0[vpn0].flags & PTE_U) {
                    void *new_page = memory_alloc_page(); // allocate a new physical page for the child
                    if (!new_page) {
                        panic("Failed to allocate memory for user page");
                    }

                    void *parent_page = pagenum_to_pageptr(parent_pt0[vpn0].ppn); // get the physical page pointer
                    memcpy(new_page, parent_page, PAGE_SIZE); // copy the physical page

                    // Map the new page in the child PT0
                    child_pt0[vpn0] = leaf_pte(new_page, parent_pt0[vpn0].flags); // set the new child pt0's flags as the leaf pte
                } 
            }
        }
    }

    // construct SATP tag for the child process
    uintptr_t new_mtag = ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
                         ((uintptr_t)asid << RISCV_SATP_ASID_shift) |
                         pageptr_to_pagenum(child_root); 

    sfence_vma(); // tlb flush
    return new_mtag;
}
