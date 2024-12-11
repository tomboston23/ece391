//           vioblk.c - VirtIO serial port (console)
//          

#include "virtio.h"
#include "intr.h"
#include "halt.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "error.h"
#include "string.h"
#include "thread.h"
#include "lock.h"


//           COMPILE-TIME PARAMETERS
//          

#define VIOBLK_IRQ_PRIO 1

// Assumption: The system will crash on errors. Locks are not explicitly released in error paths.
static struct lock vio_lock;

//           INTERNAL CONSTANT DEFINITIONS
//          

//           VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_BLK_F_DISCARD        13
#define VIRTIO_BLK_F_WRITE_ZEROES   14

//           INTERNAL TYPE DEFINITIONS
//          

//           All VirtIO block device requests consist of a request header, defined below,
//           followed by data, followed by a status byte. The header is device-read-only,
//           the data may be device-read-only or device-written (depending on request
//           type), and the status byte is device-written.

struct vioblk_request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

//           Request type (for vioblk_request_header)

#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1

//           Status byte values

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

//           Main device structure.
//          
//           FIXME You may modify this structure in any way you want. It is given as a
//           hint to help you, but you may have your own (better!) way of doing things.

// static struct lock vioblk_lock;

struct vioblk_device {
    volatile struct virtio_mmio_regs * regs;
    struct io_intf io_intf;
    uint16_t instno;
    uint16_t irqno;
    int8_t opened;
    int8_t readonly;

    //           optimal block size
    uint32_t blksz;
    //           current position
    uint64_t pos;
    //           sizeo of device in bytes
    uint64_t size;
    //           size of device in blksz blocks
    uint64_t blkcnt;

    struct {
        //           signaled from ISR
        struct condition used_updated;

        //           We use a simple scheme of one transaction at a time.

        union {
            struct virtq_avail avail;
            char _avail_filler[VIRTQ_AVAIL_SIZE(1)];
        };

        union {
            volatile struct virtq_used used;
            char _used_filler[VIRTQ_USED_SIZE(1)];
        };

        //           The first descriptor is an indirect descriptor and is the one used in
        //           the avail and used rings. The second descriptor points to the header,
        //           the third points to the data, and the fourth to the status byte.

        struct virtq_desc desc[4];
        struct vioblk_request_header req_header;
        uint8_t req_status;
    } vq;

    //           Block currently in block buffer
    uint64_t bufblkno;
    //           Block buffer
    char * blkbuf;

    struct lock dev_lock;
};

//           INTERNAL FUNCTION DECLARATIONS
//          

static int vioblk_open(struct io_intf ** ioptr, void * aux);

static void vioblk_close(struct io_intf * io);

static long vioblk_read (
    struct io_intf * restrict io,
    void * restrict buf,
    unsigned long bufsz);

static long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n);

static int vioblk_ioctl (
    struct io_intf * restrict io, int cmd, void * restrict arg);

static void vioblk_isr(int irqno, void * aux);

//           IOCTLs

static int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr);
static int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr);
static int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr);
static int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr);

//           EXPORTED FUNCTION DEFINITIONS
//          

//           Attaches a VirtIO block device. Declared and called directly from virtio.c.

/*Initializes the virtio block device with the necessary IO operation functions and sets the required
feature bits. Also fills out the descriptors in the virtq struct. It attaches the virtq avail and
virtq used structs using the virtio attach virtq function. Finally, the interupt service routine
and device are registered.*/

/*vioblk_attach:
    initializes IO functions and negotiates features
    fills descriptors so that we don't have to do it in read/write
    set vioblk_device variables:
        - regs (given)
        - blksz (given)
        - blkcnt (from regs)
        - size (blkcnt * blksz)
        - blkbuf (points directly below itself in the struct)
        - irqno (given)
        - io_intf (initialized at the top of the function)
    set descriptors
    reset virtq using the given virtio_reset_virtq funciton. This is called in open so it's probably redundant
    reset idx and flags for avail and used vq's - also done in open so probably unnecessary
    attach virtqueues
    initialize used_updated condition - I enabled and disabled interrupts before/after but not sure if it's needed
    register isr, register device and set driver_ok status to show it was attached

*/
void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    //           FIXME add additional declarations here if needed
    //initialize virtio_ops according to functions below

    static const struct io_ops virtio_ops = {
        .close = vioblk_close,
        .read = vioblk_read,
        .write = vioblk_write,
        .ctl = vioblk_ioctl
    };
    
    //regs->status |= VIRTIO_STAT_ACKNOWLEDGE; <- this is not needed because this bit is already set in virtio.c
    /*help me with this part*/

    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct vioblk_device * dev;
    uint_fast32_t blksz;
    int result;

    assert (regs->device_id == VIRTIO_ID_BLOCK);

    //           Signal device that we found a driver
    regs->status |= VIRTIO_STAT_ACKNOWLEDGE; 
    //I dont think this is necessary since it's called in virtio.c but just to be safe
    regs->status |= VIRTIO_STAT_DRIVER;
    //           fence o,io
    __sync_synchronize();

    //           Negotiate features. We need:
    //            - VIRTIO_F_RING_RESET and
    //            - VIRTIO_F_INDIRECT_DESC
    //           We want:
    //            - VIRTIO_BLK_F_BLK_SIZE and
    //            - VIRTIO_BLK_F_TOPOLOGY.

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);

    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    //           If the device provides a block size, use it. Otherwise, use 512.

    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

    debug("%p: virtio block device block size is %lu", regs, (long)blksz);

    //           Allocate initialize device struct

    dev = kmalloc(sizeof(struct vioblk_device) + blksz);
    memset(dev, 0, sizeof(struct vioblk_device));
    //           FIXME Finish initialization of vioblk device here

    lock_init(&vio_lock, "vioblk_lock");
    dev->regs = regs;   //attach regs
    dev->blkcnt = regs->config.blk.capacity;
    dev->blksz = blksz; //set block size
    dev->size = dev->blkcnt * dev->blksz;
    dev->blkbuf = (char*)&(dev->blkbuf) + sizeof(char*);
    dev->irqno = irqno; //set irqno
    dev->io_intf.ops = &virtio_ops; //set io ops

    //set descriptors
    //the first is the indirect descriptor so set indirect flag - size should be the size of the three other descriptors
    dev->vq.desc[0] = (struct virtq_desc){(uint64_t)&dev->vq.desc[1], 3 * sizeof(struct virtq_desc), VIRTQ_DESC_F_INDIRECT, 0};
    //second descriptor points to request header. Should include next flag and point to next desc
    dev->vq.desc[1] = (struct virtq_desc){(uint64_t)&dev->vq.req_header, sizeof(struct vioblk_request_header), VIRTQ_DESC_F_NEXT, 1};
    //third descriptor points to buffer. Should include next flag and point to the next descriptor
    dev->vq.desc[2] = (struct virtq_desc){(uint64_t)dev->blkbuf, dev->blksz, VIRTQ_DESC_F_NEXT, 2}; 
    //last descriptor points to request status - size should be size of req_status. Includes write only flag
    dev->vq.desc[3] = (struct virtq_desc){(uint64_t)&dev->vq.req_status, sizeof(dev->vq.req_status), VIRTQ_DESC_F_WRITE, 0};
    //           The first descriptor is an indirect descriptor and is the one used in
    //           the avail and used rings. The second descriptor points to the header,
    //           the third points to the data, and the fourth to the status byte.

    //reset avail virtq - also called in open so probably redundant
    virtio_reset_virtq(dev->regs, 0);

    dev->vq.avail.idx = 0; //reset idx
    //reset flags
    dev->vq.avail.flags = 0;
    dev->vq.used.flags = 0;

    __sync_synchronize();

    //attach virtqueues to the register flags
    virtio_attach_virtq(regs, 0, 1, (uint64_t)(&dev->vq.desc), (uint64_t) (&dev->vq.used), (uint64_t) (&dev->vq.avail));

    int s = intr_disable(); //disable and enable interrupts - probably unnescessary
    condition_init(&dev->vq.used_updated, "used_updated"); //initialize condition
    intr_restore(s);

    intr_register_isr(irqno, VIOBLK_IRQ_PRIO, vioblk_isr, dev); 
    //setting aux to dev ensures we have access to the block device within the isr
    //register isr

    //lecture slides say interrupts shouldn't be enabled during attaching
    //intr_enable_irq(irqno);
    //enable interrupts for vioblk
    int instno = device_register("blk", &vioblk_open, dev); //this ensures open gets called
    //you NEED to include name as it makes sure it gets registered and also increments instance no.
    //And the name HAS to be "blk" according to main_shell.c
    dev->instno = (uint16_t)instno; //assign instno

    regs->status |= VIRTIO_STAT_DRIVER_OK;   //set driver_ok to show this was correctly initialized
    //           fence o,oi
    __sync_synchronize();
}

/*
Sets the virtq avail and virtq used queues such that they are available for use. (Hint, read
virtio.h) Enables the interupt line for the virtio device and sets necessary flags in vioblk device.
Returns the IO operations to ioptr.
*/
/*
vioblk open:
    gets the dev ptr which was passed in to aux when registering the device in attach
    checks if null and that it hasn't been opened already
    set ioptr to the io functions in the device
    open by setting opened variable to 1
    enable avail virtq by calling the given virtio_enable_virtq function using 0 as the param to signify avail vq
    set idx to 0 and set flags to 0
    enable interrupts
    return 0 to signify success - main will flag an error if we return anything else
*/
int vioblk_open(struct io_intf ** ioptr, void * aux) {
    //           FIXME your code here
    /*
    This is modeled very closely to uart open
    */
    if(aux == NULL) return -ENODEV; //check for NULL

    struct vioblk_device * const dev = (struct vioblk_device *) aux;
    //get dev ptr

    if (dev->opened) return -EBUSY;
    //make sure it isn't already open        

    *ioptr = &dev->io_intf;
    dev->io_intf.refcnt = 1;

    dev->opened = 1; //communicate that it is open

    virtio_enable_virtq(dev->regs, 0); 
    //enable avail virtq
    
    
    dev->vq.avail.idx = 0; //resets avail idx to 0
    __sync_synchronize();
    //reset flags - also done in attach so probably not needed
    dev->vq.used.flags = 0;
    dev->vq.avail.flags = 0; 
    intr_enable_irq(dev->irqno); //enable interrupts for our irq

    return 0; // MUST return 0

}

//           Must be called with interrupts enabled to ensure there are no pending
//           interrupts (ISR will not execute after closing).
/*
Resets the virtq avail and virtq used queues and sets necessary flags in vioblk device
*/
/*
close gets dev from the io ptr using the offset like ioctl
disables interrupts for our irqno
resets virtq using the given virtio_reset_virtq function -- pass in 0 to signify the avail vq
close vioblk by setting opened to 0
*/

void vioblk_close(struct io_intf * io) {
    //           FIXME your code here

    //get device from offset same way they did in ioctl
    struct vioblk_device * const dev = (void*)io - offsetof(struct vioblk_device, io_intf);
    
    //intr_disable_irq(dev->irqno); 
    //got this line from uart close

    if (dev->io_intf.refcnt > 0) {
        dev->io_intf.refcnt--;
    }

    // Only reset and close if no more references exist
    if (dev->io_intf.refcnt == 0) {
        intr_disable_irq(dev->irqno); 
        virtio_reset_virtq(dev->regs, 0); // reset virtqueue
        dev->opened = 0; // close
    }

    // virtio_reset_virtq(dev->regs, 0); //reset virtqueue

    // dev->opened = 0;//close


}

/*
Reads bufsz number of bytes from the disk and writes them to buf. Achieves this by repeatedly
setting the appropriate registers to request a block from the disk, waiting until the data has been
populated in block buffer cache, and then writes that data out to buf. Thread sleeps while waiting for
the disk to service the request. Returns the number of bytes successfully read from the disk.
*/

/*vioblk_read:
    Gets device, checks for errors, and sets necessary flags for reading
    For each iteration:
        - get position in sector and the sector number
        - set header for the descriptors to use
        - set avail ring idx and head, notify the regs there is an available virtqueue
        - Disable interrupts and wait for the device to service the request, then re-enable
        - Check for error
        - Set bytes_to_read, which will be min(ev->blksz - sectorpos, bufsz - bytes_read)
        - copy bytes_to_read bytes from device blkbuf into buf, then increment bytes_read and pos

    Overall effects:
        memory copied from device into buf
        pos and bufblkno changed
        returned bytes_read
    
*/

long vioblk_read (
    struct io_intf * restrict io,
    void * restrict buf,
    unsigned long bufsz)
{
    //           FIXME your code here

    struct vioblk_device *dev = (void*)io - offsetof(struct vioblk_device, io_intf); //get device - same way they did in ioctl
    if (dev->pos >= dev->size || bufsz == 0 || dev->opened == 0) return 0;           //check for obvious errors
    lock_acquire(&vio_lock);
    unsigned long bytes_read = 0; //initialize counter
    dev->vq.desc[2].flags |= VIRTQ_DESC_F_WRITE;  // Set device to write data into driver buffer

    while (bufsz > bytes_read) { //loop until we've read enough bytes
        dev->bufblkno = dev->pos / dev->blksz;  //get sector number
        unsigned long sectorpos = dev->pos % dev->blksz;     //get position in the sector

        //header is used by the descriptors
        dev->vq.req_header.type = VIRTIO_BLK_T_IN;          //set header for data IN (read)
        dev->vq.req_header.sector = dev->bufblkno;          //set sector number for header
         
        //clear head and increment idx
        dev->vq.avail.ring[0] = 0;
        //__sync_synchronize();
        dev->vq.avail.idx += 1;
        //__sync_synchronize();

        virtio_notify_avail(dev->regs, 0);//notify there is an available virtqueue

        //disable interrupts
        int s = intr_disable();
        while (dev->vq.used.idx != dev->vq.avail.idx) condition_wait(&dev->vq.used_updated); //wait for the device to service request
        intr_restore(s);
        //restore interrupts after condition satisfied
        if (dev->vq.req_status != VIRTIO_BLK_S_OK) return -EIO;//ensure success

        //determine bytes_to_read - will never be greater than 512 since blksz - sectorpos maxes out at 512.
        //uses the min of blksz - sectorpos and bufsz - bytes_read
        //the first deals with the case where we start after the beginning of the sector
        //the second deals with the case where we end prior to the end of a sector
        unsigned long bytes_to_read = (dev->blksz - sectorpos < bufsz - bytes_read) ? dev->blksz - sectorpos : bufsz - bytes_read;
        //copy bytes_to_read bytes into the buffer, starting at blkbuf + sectorpos
        memcpy(buf + bytes_read, dev->blkbuf + sectorpos, bytes_to_read);

        bytes_read += bytes_to_read; //increment bytes read
        dev->pos += bytes_to_read; //increment position
    }

    lock_release(&vio_lock);
    return bytes_read;
}


/*
Writes n number of bytes from the parameter buf to the disk. The size of the virtio device should
not change. You should only overwrite existing data. Write should also not create any new files.
Achieves this by filling up the block buffer cache and then setting the appropriate registers to request
the disk write the contents of the cache to the specified block location. Thread sleeps while waiting
for the disk to service the request. Returns the number of bytes successfully written to the disk.
*/

/*vioblk continually writes blocks of up to dev->blksz length until n bytes have been written
    - Sets header for receiving data (VIRTIO_BLK_T_OUT) which is passed in descriptors already
    - Set flags to write mode
    - initialize sector position, bufblkno, avail ring, idx for each loop iteration
    - disable interrupts, sleep the thread until the request has been processed, then re-enable interrupts
    - Check for errors then determine the number of bytes to write for the iteration
    - copy memory over to the device, dealing with the simple case or annoying cases
    - increment count and pos

    Overall effect:
        memory copied from buf into device
        bufblkno and pos changed
        number of bytes written is returned
*/

long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n)
{
    //           FIXME your code here

    struct vioblk_device *dev = (void*)io - offsetof(struct vioblk_device, io_intf); //get device using offsetof
    if (dev->pos >= dev->size) return 0; //make sure pos isn't too big

    lock_acquire(&vio_lock);
    unsigned long bytes_written = 0; //initialize counter
    dev->vq.desc[2].flags &= ~VIRTQ_DESC_F_WRITE;  // Set to write mode
    char buffer[dev->blksz];//set up buffer for copying data

    while (n > bytes_written) {
        dev->bufblkno = dev->pos / dev->blksz; //get sector no.
        unsigned long sectorpos = dev->pos % dev->blksz; //get sector pos
        unsigned long bytes_to_write = (dev->blksz - sectorpos < n - bytes_written) ? dev->blksz - sectorpos : n - bytes_written;
        //determine number of bytes to write
        //this is min(blksz - sectorpos, n - bytes_written)
        //The first arg will never be greater than 512, so our upper bound is valid
        //The sectorpos deals with cases where we start partway through a sector
        //The n-bytes_written part deals with cases where we are reaching n bytes written 
        //and need to stop prior to the end of a sector
        if (sectorpos > 0 || bytes_to_write < dev->blksz) {
            //this refers to the annoying cases where we start or end in the middle of a sector
            lock_release(&vio_lock);
            if(vioblk_read(io, buffer, dev->blksz) < 0){
                return -EIO; 
            } 
            lock_acquire(&vio_lock);
            memcpy(buffer + sectorpos, buf+bytes_written, bytes_to_write); 
            memcpy(dev->blkbuf + sectorpos, buffer, dev->blksz);
            
        } else { //this refers to the ideal case where we write from the start of a sector to the end of a sector
            memcpy(dev->blkbuf, buf + bytes_written, bytes_to_write);
        }
        dev->vq.desc[2].flags &= ~VIRTQ_DESC_F_WRITE;  // Set to write mode

        //set req header for descriptors to pass
        dev->vq.req_header.type = VIRTIO_BLK_T_OUT;
        dev->vq.req_header.sector = dev->bufblkno; 
        //reset avail buffer
        dev->vq.avail.ring[0] = 0;
        __sync_synchronize();
        //increment idx
        dev->vq.avail.idx += 1;
        __sync_synchronize();
        //notify the device that there is a new available virtqueue
        virtio_notify_avail(dev->regs, 0);

        //disable interrupts before suspending thread
        int s = intr_disable();
        while (dev->vq.used.idx != dev->vq.avail.idx) { //loop until indices match
            condition_wait(&dev->vq.used_updated); //wait for disk to process request
        }
        //restore interrupts to resume execution
        intr_restore(s);

        //check if an error occurred
        if (dev->vq.req_status != VIRTIO_BLK_S_OK) return -EIO;

        //increment count and pos by number of bytes written
        bytes_written += bytes_to_write;
        dev->pos += bytes_to_write;
    }
    lock_release(&vio_lock);
    return bytes_written; //return number of bytes written
}

int vioblk_ioctl(struct io_intf * restrict io, int cmd, void * restrict arg) {
    struct vioblk_device * const dev = (void*)io -
        offsetof(struct vioblk_device, io_intf);

    //trace("%s(cmd=%d,arg=%p)", __func__, cmd, arg);
    int ret = -1;

    switch (cmd) {
    case IOCTL_GETLEN:
        ret = vioblk_getlen(dev, arg);
    case IOCTL_GETPOS:
        ret = vioblk_getpos(dev, arg);
    case IOCTL_SETPOS:
        lock_acquire(&vio_lock);
        ret = vioblk_setpos(dev, arg);
        lock_release(&vio_lock);
    case IOCTL_GETBLKSZ:
        ret = vioblk_getblksz(dev, arg);
    }
    return ret;
}


/*interrupt service routine:

gets dev from aux ptr, then checks for interrupt bit 0. We have to acknowledge the interrupt and broadcast
the condition used_updated to let the driver know that there are more used virtqueues. This is what allows
read and write to continually happen.

Side effects : dev->regs->interrupt_ack changed - The overall effect is small, it just means the system knows
that the interrupt has been taken care of
*/
void vioblk_isr(int irqno, void * aux) {
    //           FIXME your code here

    struct vioblk_device * dev = aux;//get device
    //The device works without dealing with interrupt bit 1 but let's include it anyway

    if(dev->regs->interrupt_status & (1<<1)){ //check for bit 1 interrupt
        //device used a buffer in one of the virtqueues
        //that means used has been updated
        __sync_synchronize();
        condition_broadcast(&dev->vq.used_updated); //broadcast change in used vq
        dev->regs->interrupt_ack |= (1<<1);         //acknowledge interrupt
    }
    if(dev->regs->interrupt_status & (1<<0)){ //check for bit 0 interrupt
        //the interrupt was asserted because the configuration of the device has changed.
        //no idea what this means but at least acknowledge it. No clue if this will ever occur
         __sync_synchronize();
        condition_broadcast(&dev->vq.used_updated); //signify that used is updated
        dev->regs->interrupt_ack |= (1<<0); //acknowledge interrupt
    }
    __sync_synchronize();

}


/*EVERYTHING BELOW THIS POINT IS FINISHED*/


/*gets the length of the vioblk data and puts it in lenptr. This is stored in the size struct variable

Returns 0 to signal it was succesful
Should return invalid if either input variable is NULL
*/

int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr) {
    //           FIXME your code here
    if (lenptr == NULL || dev == NULL) return -EINVAL;
    *lenptr = dev->size; //get the size and put in lenptr


    return 0; //return 0 to signal that it was successful
}

/*gets the position of the data that our device is currently pointing to.

  puts pos into the posptr.
  Returns 0 to signal successful execution
  Return invalid if either input ptr is NULL
*/
int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr) {
    //           FIXME your code here
    if (posptr == NULL || dev == NULL) return -EINVAL;
    *posptr = dev->pos; // put pos in posptr



    return 0; //return 0 to signal that it was successful
}
/*Set pos:
    sets the device pos variable to the value passed in posptr
    returns 0 if successful, returns invalid if the input size is too large or if inputs are null
*/
int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr) {
    //           FIXME your code here
    if (posptr == NULL || dev == NULL) return -EINVAL;
    uint64_t pos = *posptr;
    if(pos > dev->size) return -EINVAL; //make sure pos is in the data - return invalid if it's too large
    dev->pos = pos; //set pos


    return 0; //return 0 to signal that it was successful
}

/*
fetches the device's blksz from the dev struct. 
returns 0 to signify successful operation
will return invalid if input ptrs are null
*/
int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr)
{
    //           FIXME your code here
    if (blkszptr == NULL || dev == NULL) return -EINVAL;
    *blkszptr = dev->blksz; //get blksz from the struct


    return 0;  //return 0 to signal that it was successful
}