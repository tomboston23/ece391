#include "trap.h"
#include "scnum.h"
#include <stddef.h>
#include "process.h"
#include "memory.h"
#include "fs.h"
#include "device.h"
#include "console.h"
#include "error.h"


// Description: Prints a message to the console.
// Parameters:
//   - msg: Pointer to the null-terminated string to be printed.
// Returns:
//   - 0 on success.
//   - -1 if the validation of the user pointer fails (commented out).

static int sysmsgout(const char *msg) {
    // if(memory_validate_vstr(msg, PTE_U) != 0) {
    //     return -1;
    // }

    console_printf(msg); // write a message to the current device's console

    return 0; // Success
}

// Description: Opens a device at the specified file descriptor.
// Parameters:
//   - fd: File descriptor index.
//   - name: Name of the device to open.
//   - instno: Instance number of the device.
// Returns:
//   - 0 on success.
//   - -1 on failure (invalid FD, validation failure, or device open error).
static int sysdevopen(int fd, const char *name, int instno) {
    struct process *proc = current_process(); // get current process
    if(fd < 0 || fd >= PROCESS_IOMAX || proc->iotab[fd] != NULL) { // boundary checks
        return -1; // Invalid file descriptor
    }

    // if(memory_validate_vstr(name, PTE_U) != 0) {
    //     return -1; 
    // }

    struct io_intf *dev_io; // fake device pointer
    if(device_open(&dev_io, name, instno) != 0) { // open device with deviceio functions (vioblk, uart, etc.)
        return -1; // Failed to open device
    }

    proc->iotab[fd] = dev_io; // set the iotable at the file descriptor fd to dev_io
    return 0; // Success
}

// Function: sysfsopen
// Description: Opens a file in the file system at the specified file descriptor.
// Parameters:
//   - fd: File descriptor index.
//   - name: Name of the file to open.
// Returns:
//   - 0 on success.
//   - -1 on failure (invalid FD, validation failure, or file open error).
static int sysfsopen(int fd, const char *name) {
    struct process *proc = current_process(); //get current process
    if(fd < 0 || fd >= PROCESS_IOMAX || proc->iotab[fd] != NULL) { // boundary checks
        return -1; // Invalid file descriptor
    }

    // if(memory_validate_vstr(name, PTE_U) != 0) {
    //     return -1; 
    // }

    struct io_intf *fs_io; // fake file io pointer
    int ret = fs_open(name, &fs_io); // open file using fs_open
    if(ret != 0) {
        return -1; // Failed to open device
    }

    proc->iotab[fd] = fs_io; // set the iotable at the file descriptor fd to fs_io
    return 0; // Success
}


// Function: sysclose
// Description: Closes the device or file at the specified file descriptor.
// Parameters:
//   - fd: File descriptor index to close.
// Returns:
//   - 0 on success.
//   - -1 if the file descriptor is invalid or not in use.
static int sysclose(int fd) {
    struct process *proc = current_process(); // get current process
    if(fd < 0 || fd >= PROCESS_IOMAX || proc->iotab[fd] == NULL) { // boundary checks
        return -1;
    }
    ioclose(proc->iotab[fd]); // close current file descriptor in io table
    proc->iotab[fd] = NULL; // set io table pointer to NULL
    return 0; // Success
}

// Function: sysexit
// Description: Exits the current process.
// Parameters: None
// Returns: This function does not return because the process is terminated.
static int sysexit(void) {
    process_exit(); // Terminate the process
    return 0; // Line never hits because of process exit
}

// Function: sysread
// Description: Reads data from an opened file descriptor into a buffer.
// Parameters:
//   - fd: File descriptor index to read from.
//   - buf: Pointer to the buffer where data will be stored.
//   - bufsz: Size of the buffer.
// Returns:
//   - Number of bytes read on success.
//   - -1 on failure.
static long sysread(int fd, void *buf, size_t bufsz) {
    // check if fd is in open file descriptor array
    // find the io associated with that fd
    struct process *proc = current_process(); // get current process
    if(fd < 0 || fd >= PROCESS_IOMAX || proc->iotab[fd] == NULL) { // boundary checks
        return -1;
    }

    // if(memory_validate_vptr_len(buf, bufsz, PTE_W | PTE_U) != 0) {
    //     return -1; // Invalid user buffer
    // }

    if(ioread_full(proc->iotab[fd], buf, bufsz) != bufsz) { // try to read from whatever io device or file
        return -1;
    }

    return bufsz; // return the bytes read
}

// Function: syswrite
// Description: Writes data from a buffer to an opened file descriptor.
// Parameters:
//   - fd: File descriptor index to write to.
//   - buf: Pointer to the buffer containing the data to write.
//   - len: Length of the data to write.
// Returns:
//   - Number of bytes written on success.
//   - -1 on failure.
static long syswrite(int fd, const void *buf, size_t len) {
    // check if fd is in open file descriptor array
    // find the io associated with that fd

    struct process *proc = current_process(); // get current process
    if(fd < 0 || fd >= PROCESS_IOMAX || proc->iotab[fd] == NULL) { // boundary checks
        return -1;
    }

    // if(memory_validate_vptr_len(buf, len, PTE_W | PTE_U) != 0) {
    //     return -1; // Invalid user buffer
    // }

    if(iowrite(proc->iotab[fd], buf, len) != len) { // try to write to whatever io device or file
        return -1;
    }

    return len; // return the bytes written
}

// Function: sysioctl
// Description: Performs an ioctl operation on a file descriptor.
// Parameters:
//   - fd: File descriptor index.
//   - cmd: Ioctl command to execute.
//   - arg: Pointer to the argument for the command.
// Returns:
//   - 0 on success.
//   - -1 on failure.
static int sysioctl(int fd, int cmd, void *arg) {
    // check if fd is in open file descriptor array
    // find the io associated with that fd

    struct process *proc = current_process(); // get current process
    if(fd < 0 || fd >= PROCESS_IOMAX || proc->iotab[fd] == NULL) { // boundary checks
        return -1;
    }

    if(ioctl(proc->iotab[fd], cmd, arg) != 0) { // ioctl functions liek get pos, set pos, etc
        return -1;
    }
    return 0;
}

// Function: sysexec
// Description: Executes a new program from the file descriptor.
// Parameters:
//   - fd: File descriptor index containing the program to execute.
// Returns:
//   - 0 on success.
//   - -1 on failure.
static int sysexec(int fd) {
    // this needs to find the executable file in the file descriptor array and execute it
    struct process *proc = current_process(); // get current process
    if(fd < 0 || fd >= PROCESS_IOMAX || proc->iotab[fd] == NULL) { // boundary checks
        return -1; // Invalid file descriptor
    }

    struct io_intf *exe_io = proc->iotab[fd]; // set the executable file to the current file descriptor file
    if(process_exec(exe_io) != 0) { // run process exec on it
        return -1;
    }
    return 0; 
}

// Function: syscall_handler
// Description: Handles system calls by routing them to the appropriate handler based on the syscall number.
// Parameters:
//   - tfr: Pointer to the trap frame containing syscall arguments and return values.
// Returns: None. The result of the syscall is stored in tfr->x[TFR_A0].
extern void syscall_handler(struct trap_frame *tfr) {
    tfr->sepc += 4; // Advance the program counter to avoid repeated syscall execution
    if (tfr == NULL) {
        kprintf("syscall_handler: NULL trap frame");
    }

    uint64_t syscall_number = tfr->x[TFR_A7]; // Get the syscall number from the tfr
    //console_printf("syscall number: %d \n", syscall_number);
    uint64_t ret;
    switch (syscall_number) {
        case SYSCALL_EXIT:
            ret = sysexit();
            break;
        case SYSCALL_MSGOUT:
            ret = sysmsgout((const char *)tfr->x[TFR_A0]);
            break;
        case SYSCALL_DEVOPEN:
            ret = sysdevopen(tfr->x[TFR_A0], (const char *)tfr->x[TFR_A1], tfr->x[TFR_A2]);
            break;
        case SYSCALL_FSOPEN:
            ret = sysfsopen(tfr->x[TFR_A0], (const char *)tfr->x[TFR_A1]);
            break;
        case SYSCALL_CLOSE:
            ret = sysclose(tfr->x[TFR_A0]);
            break;
        case SYSCALL_READ:
            ret = sysread(tfr->x[TFR_A0], (void *)tfr->x[TFR_A1], tfr->x[TFR_A2]);
            break;
        case SYSCALL_WRITE:
            ret = syswrite(tfr->x[TFR_A0], (const void *)tfr->x[TFR_A1], tfr->x[TFR_A2]);
            break;
        case SYSCALL_IOCTL:
            ret = sysioctl(tfr->x[TFR_A0], tfr->x[TFR_A1], (void *)tfr->x[TFR_A2]);
            break;
        case SYSCALL_EXEC:
            ret = sysexec(tfr->x[TFR_A0]);
            break;
        default:
            ret = -1; // Invalid Syscall
    }

    tfr->x[TFR_A0] = ret; // Store the syscall result in the return register
}