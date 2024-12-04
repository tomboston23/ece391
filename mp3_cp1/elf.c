#include "console.h"
#include "thread.h"
#include "device.h"
#include "uart.h"
#include "timer.h"
#include "intr.h"
#include "heap.h"
#include "halt.h"
#include "elf.h"
#include "string.h"
#include "io.h"
#define MEM_START 0x80100000  //this where the memory starts
#define MEM_END 0x81000000 //this where the memory ends

#define ELF0 0x7F
#define ELF1 'E'
#define ELF2 'L'
#define ELF3 'F'


//MY CODE
int elf_load(struct io_intf *io, void (**entryptr)(struct io_intf *io)) {

// FUNCTION INTERFACE
 //io is the io interface
//entryptr is the entry function that you need to start it (usually its some main function)


    kprintf("WE AT ELF LOAD BUDDY\n");
    Elf64_Ehdr elfHead; //Read and verify the ELF header
    long headRead = ioread(io, &elfHead, sizeof(elfHead));
    kprintf("HEAD SURVIDED\n");
    if(headRead < 0){
        return -EINVAL; //check if theres an error when reading the header
    }
    kprintf("Youooo im in elfload!");
    if (elfHead.e_ident[0] != ELF0 || elfHead.e_ident[1] != ELF1 || 
        elfHead.e_ident[2] != ELF2 || elfHead.e_ident[3] != ELF3) {
        kprintf("\nNOT a ELF FILE");
        return -EINVAL; //we have to check if its an elf file first
    }

    //check riscv 
    if(!(elfHead.e_machine == EM_RISCV && elfHead.e_type == ET_EXEC)){
        return -EINVAL;
    }

    //little endian
    if(elfHead.e_ident[5] != ELFDATA2LSB){
        return -EINVAL;
    }

    //seek to the prgram header table
    if(ioseek(io, elfHead.e_phoff) < 0){
        return -EINVAL;
    }


    //loop through program headers to find PT_LOAD segments
    for (int i = 0; i < elfHead.e_phnum; i++) {
        Elf64_Phdr pgrmHead;
        uint64_t pddidyOffset = elfHead.e_phoff + i*sizeof(Elf64_Phdr);
        if ((ioseek(io, pddidyOffset) < 0) || ioread(io, &pgrmHead, sizeof(Elf64_Phdr)) < 0) {
            return -EINVAL; // Error reading program header
        }
        if (pgrmHead.p_type == PT_LOAD) {
            // Map segment into memory at p_vaddr
            if (pgrmHead.p_vaddr < MEM_START || pgrmHead.p_vaddr + pgrmHead.p_memsz > MEM_END) {
                return -EINVAL; // Out-of-bounds address
            }

            if (ioseek(io,pgrmHead.p_offset) < 0) {
                return -EINVAL; // Error seeking segment
            }
            if(ioread(io, (void *)pgrmHead.p_vaddr, pgrmHead.p_filesz) < 0){
                return -EINVAL; //error loading in the program address
            } 
            if(pgrmHead.p_memsz > pgrmHead.p_filesz){
                long extraSpaceP = pgrmHead.p_memsz - pgrmHead.p_filesz;  
                memset((void*)pgrmHead.p_vaddr+pgrmHead.p_filesz, 0, extraSpaceP); // if theres more memory than file space, just zero out the remaining size
            } 
        }
    }
    
    if(elfHead.e_entry < MEM_START || elfHead.e_entry>MEM_END){
        return -EINVAL; //check if memory is in legal bounds
    }
    
    *entryptr = (void (*)(struct io_intf *io))(elfHead.e_entry);

    return 0;
}