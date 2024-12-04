#include "fs.h"
#include "string.h"

// IMPORTANT GLOBAL DECLARATIONS

boot_block_t boot_block;
file_t files[MAX_FILES];
struct io_intf* overall_io;

// Mounts the file system by initializing a global io interface and loading the boot block.
// @param io - Pointer to the io interface.
// @return 0 on success, -1 on failure.
int fs_mount(struct io_intf* io) {
    if (io == NULL) { // If io interface is not valid
        return -1;
    }
    overall_io = io; // set global io ptr

    if (ioread_full(overall_io, &boot_block, BLOCK_SIZE) != BLOCK_SIZE) { // If boot block read from storage is not the same length as boot block struct
        return -1;
    }

    if (boot_block.stats.no_dentries > MAX_DENTRIES || // Dentries overflow
        boot_block.stats.no_inodes <= 0 || // Inodes underflow
        boot_block.stats.no_datablocks <= 0) { // Datablocks underflow
        return -1; 
    }

    memset(files, 0, sizeof(files)); // Initialise files array to all zeros

    return 0;
}


// Opens a file and sets up an io interface for accessing it in the global files struct.
// @param name - Name of the file to open.
// @param io - Pointer to the io interface for the file.
// @return 0 on success, -1 on failure.
int fs_open(const char* name, struct io_intf** io) {
    if (io == NULL || name == NULL) {
        return -1;
    }

    uint32_t no_dentries = boot_block.stats.no_dentries; // Get number of entries

    dentry_t* file_dentry = NULL; // Temporary dentry pointer for the dentry of the file passed to this function

    for(int i = 0; i < no_dentries; i++) { // Iterate through dentries
        if(strcmp(boot_block.dentries[i].filename, name) == 0){ // If dentry filename is the same as name passed
            file_dentry = &boot_block.dentries[i]; // Set temp dentry pointer
            break;
        }
    }
    
    if(file_dentry == NULL){ // If there exists no file with that name, return -1 (no file could be opened)
        return -1;
    }

    file_t* available_file = NULL; // Set temporary file pointer

    for (int i = 0; i < MAX_FILES; i++) { // Iterate through file array
        if (!(files[i].flags & FILE_IN_USE)) { // Find if files[i] is in use
            available_file = &files[i]; // If not, set the file pointer to it and break
            break;
        }
    }

    if (available_file == NULL) { // If there are no available files, return -1 (all file systems are being used)
        return -1;
    }

    uint32_t inode_idx = file_dentry->inode_no; // Inode for temp file dentry
    uint64_t inode_offset = BLOCK_SIZE*(inode_idx+1); // offset from overall_io
    if (overall_io->ops->ctl(overall_io, IOCTL_SETPOS, &inode_offset) != 0) {
        return -1;
    }

    inode_t inode; // Inode struct to fill
    if (ioread_full(overall_io, &inode, sizeof(inode_t)) != sizeof(inode_t)) { // Read that dentry's inode into the fillable struct
        return -1;
    }

    static const struct io_ops file_io_ops = { // set the ops struct
        .close = fs_close,
        .read = fs_read,
        .write = fs_write,
        .ctl = fs_ioctl
    };

    available_file->io.ops = &file_io_ops; // connect the ops struct
    available_file->inode_no = file_dentry->inode_no; // Set the temp file inode to the dentry inode
    available_file->file_size = inode.byte_length; // Calculate file size in bytes
    available_file->file_pos = 0; // Start at the beginning of the file
    available_file->flags = FILE_IN_USE; // Mark as in use
    *io = &available_file->io; // set pointer to io pointer to the avaiable_file's io pointer

    return 0;

}


// Closes a file and releases the associated file in the global files list.
// @param io - Pointer to the io interface of the file to close.
void fs_close(struct io_intf* io) {
    if (io == NULL) {
        return;
    }

    file_t* file_to_close = NULL; // Temporary file pointer to find the file to close 

    for (int i = 0; i < MAX_FILES; i++) { // iterate through the open files array
        if (&files[i].io == io && (files[i].flags & FILE_IN_USE)) { // if the file io interfaces match and the file is in use
            file_to_close = &files[i]; // set the file pointer to that file and break
            break;
        }
    }

    if (file_to_close == NULL) { // If there isn't a match (file isn't in the array/isn't open), return -1
        return;
    }

    file_to_close->flags &= ~FILE_IN_USE; // set this file location to not in use

}


// Writes data to a file starting from its current position (as saved in the file list).
// @param io - Pointer to the io interface of the file.
// @param buf - Pointer to the buffer containing data to write.
// @param n - Number of bytes to write.
// @return Number of bytes written, -1 on failure.
long fs_write(struct io_intf* io, const void* buf, unsigned long n) {
    if (io == NULL || buf == NULL) {
        return -1;
    }

    file_t* file_to_write = NULL; // Temporary file pointer to find the file to write to 

    for (int i = 0; i < MAX_FILES; i++) { // iterate through the open files array
        if (&files[i].io == io && (files[i].flags & FILE_IN_USE)) { // if the file io interfaces match and the file is in use
            file_to_write = &files[i]; // set the file pointer to that file and break
            break;
        }
    }

    if (file_to_write == NULL) { // If there isn't a match (the file to write to isn't open), return -1
        return -1;
    }

    if (file_to_write->file_pos >= file_to_write->file_size) { // boundary checking
        return 0; 
    }

    if (file_to_write->file_pos + n > file_to_write->file_size) {
        n = file_to_write->file_size - file_to_write->file_pos;
    }

    uint32_t inode_idx = file_to_write->inode_no; // Inode for temp file
    uint64_t inode_offset = BLOCK_SIZE*(inode_idx+1); // offset from overall_io
    if (overall_io->ops->ctl(overall_io, IOCTL_SETPOS, &inode_offset) != 0) { // move overall_io to inode base address
        return -1;
    }

    inode_t inode; // Inode struct to fill
    if (ioread_full(overall_io, &inode, sizeof(inode_t)) != sizeof(inode_t)) { // Read that file's inode into the fillable struct
        return -1;
    }

    uint64_t bytes_written = 0; // bytes that have been written
    uint64_t remaining = n; // bytes remaining to write
    uint64_t file_pos = file_to_write->file_pos; // create a local variable to edit file_pos

    while(remaining > 0) { // ends when there are no more bytes to write 
        uint64_t datablock_idx = file_pos / BLOCK_SIZE; // datablock index calculation
        uint32_t datablock_offset = file_pos % BLOCK_SIZE; // position in each datablock
        uint64_t offset = (1 + boot_block.stats.no_inodes + inode.datablock_nos[datablock_idx])*BLOCK_SIZE; // offset of whole blocks
        uint64_t write_offset = offset + datablock_offset; // offset of whole blocks + datablock position

        uint64_t bytes_to_write_iter = (BLOCK_SIZE - datablock_offset) < remaining ? (BLOCK_SIZE - datablock_offset) : remaining; // ternary operator for how many bytes will be written in this iteration

        if (overall_io->ops->ctl(overall_io, IOCTL_SETPOS, &write_offset) != 0) { // move the global io pointer to the write offset
            return -1;
        }

        if (iowrite(overall_io, buf + bytes_written, bytes_to_write_iter) != bytes_to_write_iter) { // write the bytes from the buffer
            return -1;
        }  
        bytes_written += bytes_to_write_iter; // increment total bytes written
        remaining -= bytes_to_write_iter; // decrement bytes remaining to write
        file_pos += bytes_to_write_iter; // increment file position
    }

    file_to_write->file_pos = file_pos; // set local file position variable to files array struct file position
    return bytes_written; // return bytes written
}


// Reads data from a file starting from its current position (as saved on the file list).
// @param io - Pointer to the io interface of the file.
// @param buf - Pointer to the buffer to store read data.
// @param n - Number of bytes to read.
// @return Number of bytes read, -1 on failure.
long fs_read(struct io_intf* io, void* buf, unsigned long n) {
    if (io == NULL || buf == NULL) {
        return -1;
    }

    file_t* file_to_read = NULL; // Temporary file pointer to find the file to write to 

    for (int i = 0; i < MAX_FILES; i++) { // iterate through the open files array
        if (&files[i].io == io && (files[i].flags & FILE_IN_USE)) { // if the file io interfaces match and the file is in use
            file_to_read = &files[i]; // set the file pointer to that file and break
            break;
        }
    }

    if (file_to_read == NULL) { // If there isn't a match (the file to write to isn't open), return -1
        return -1;
    }

    if (file_to_read->file_pos >= file_to_read->file_size) { // boundary checking
        return 0;
    }

    if (file_to_read->file_pos + n > file_to_read->file_size) {
        n = file_to_read->file_size - file_to_read->file_pos;
    }

    uint32_t inode_idx = file_to_read->inode_no; // Inode for temp file
    uint64_t inode_offset = BLOCK_SIZE*(inode_idx+1); // offset from overall_io
    if (overall_io->ops->ctl(overall_io, IOCTL_SETPOS, &inode_offset) != 0) {
        return -1;
    }

    inode_t inode; // Inode struct to fill
    if (ioread_full(overall_io, &inode, sizeof(inode_t)) != sizeof(inode_t)) { // Read that file's inode into the fillable struct
        return -1;
    }

    uint64_t bytes_read = 0; // bytes that have been read
    uint64_t remaining = n; // bytes remaining to read
    uint64_t file_pos = file_to_read->file_pos; // create a local variable to edit file_pos

    while(remaining > 0) { // ends when there are no more bytes to write
       uint64_t datablock_idx = file_pos / BLOCK_SIZE; // datablock index calculation
        uint32_t datablock_offset = file_pos % BLOCK_SIZE; // position in each datablock
        uint64_t offset = (1 + boot_block.stats.no_inodes + inode.datablock_nos[datablock_idx])*BLOCK_SIZE; // offset of whole blocks
        uint64_t read_offset = offset + datablock_offset; // offset of whole blocks + datablock position


        uint64_t bytes_to_read_iter = (BLOCK_SIZE - datablock_offset) < remaining ? (BLOCK_SIZE - datablock_offset) : remaining; // ternary operator for how many bytes will be read in this iteration

        if (overall_io->ops->ctl(overall_io, IOCTL_SETPOS, &read_offset) != 0) { // move the global io pointer to the write offset
            return -1;
        }

        if (ioread_full(overall_io, buf + bytes_read, bytes_to_read_iter) != bytes_to_read_iter) {  // write the bytes to the buffer
            return -1;
        }  
        bytes_read += bytes_to_read_iter; // increment total bytes written
        remaining -= bytes_to_read_iter; // decrement bytes remaining to write
        file_pos += bytes_to_read_iter; // increment file position
    }


    file_to_read->file_pos = file_pos; // set local file position variable to files array struct file position
    return bytes_read; // return bytes written


}


// Performs an io control operation on a file (check the helpers to see what controls).
// @param io - Pointer to the io interface of the file.
// @param cmd - io control command (Set as a Macro).
// @param arg - Pointer to the argument for the command.
// @return 0 on success, -1 on failure.
int fs_ioctl(struct io_intf* io, int cmd, void* arg) {
    if (io == NULL || arg == NULL) {
        return -1;
    }

    file_t* available_file = NULL; // Set temporary file pointer

    for (int i = 0; i < MAX_FILES; i++) { // Iterate through file array
        if (&files[i].io == io && (files[i].flags & FILE_IN_USE)) { // Find if files[i] is in use
            available_file = &files[i]; // If not, set the file pointer to it and break
            break;
        }
    }

    if (available_file == NULL) { // If there are no available files, return -1 (all file systems are being used)
        return -1;
    }

    if(cmd == IOCTL_GETLEN){
        return fs_getlen(available_file, arg);
    } else if(cmd == IOCTL_GETPOS) {
        return fs_getpos(available_file, arg);
    } else if(cmd == IOCTL_SETPOS) {
        return fs_setpos(available_file, arg);
    } else if(cmd == IOCTL_GETBLKSZ) {
        return fs_getblksz(available_file, arg);
    }
    return -1;
}


// Retrieves the length of a file.
// @param fd - Pointer to the file structure.
// @param arg - Pointer to store the file length.
// @return 0 on success, -1 on failure.
int fs_getlen(file_t* fd, void* arg) {
    if(fd == NULL || arg == NULL){ // null check
        return -1;
    }
    *(uint64_t*)arg = fd->file_size; // sets arg to file size
    return 0;
}

// Retrieves the current position of a file.
// @param fd - Pointer to the file structure.
// @param arg - Pointer to store the file position.
// @return 0 on success, -1 on failure.
int fs_getpos(file_t* fd, void* arg) {
    if(fd == NULL || arg == NULL){ // null check
        return -1;
    }
    *(uint64_t*)arg =  fd->file_pos; // sets arg to file pos
    return 0;
}


// Sets the current position of a file.
// @param fd - Pointer to the file structure.
// @param arg - Pointer to the new position.
// @return 0 on success, -1 on failure.
int fs_setpos(file_t* fd, void* arg) {
    if(fd == NULL || arg == NULL){ // null check
        return -1;
    }
    uint64_t new_pos = *(uint64_t*)arg; // stores pos temporarily
    if (new_pos > fd->file_size || new_pos < 0) { // checks if new pos exceeds file size
        return -1; 
    }
    fd->file_pos = new_pos; // sets file_pos to new_pos
    return 0;
}


// Retrieves the block size used in the file system.
// @param fd - Pointer to the file structure.
// @param arg - Pointer to store the block size.
// @return 0 on success, -1 on failure.
int fs_getblksz(file_t* fd, void* arg) {
    if(fd == NULL || arg == NULL){ //null check
        return -1;
    }
    *(uint32_t*)arg = BLOCK_SIZE; // sets arg to block size
    return 0;
}