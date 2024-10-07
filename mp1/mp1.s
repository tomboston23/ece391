# mp1.s - Your solution goes here
#
        .section .data                    # Data section (optional for global data)
        .extern skyline_beacon             # Declare the external global variable

        .global skyline_win_list
        .type skyline_win_list, @object

        .global skyline_star_cnd
        .type   skyline_star_cnd, @object

        .text
        .global start_beacon
        .type   start_beacon, @function

        .text
        .global draw_beacon
        .type   draw_beacon, @function

        .text
        .global add_window
        .type   add_window, @function

        .text
        .global remove_window
        .type   remove_window, @function   

        .text
        .global draw_window
        .type   draw_window, @function  

        .text 
        .global draw_star
        .type   draw_star, @function    

        .text
        .global add_star
        .type   add_star, @function

        .text
        .global remove_star
        .type   remove_star, @function


        .equ fbuf_width, 640 #640 for demo, 10 for test
        .equ fbuf_height, 480 #480 for demo, 10 for test
        .equ max_stars, 1000  #should always be 1000


# struct skyline_window                 [16 BYTES TOTAL]
#       skyline_window * next (8 bytes) -- offset = 0
#       uint16_t x;           (2 bytes) -- offset = 8
#       uint16_t y;           (2 bytes) -- offset = 10
#       uint8_t w;            (1 byte)  -- offset = 12
#       uint8_t h;            (1 byte)  -- offset = 13
#       uint16_t color;       (2 bytes) -- offset = 14


# void add_star(uint16_t x, uint16_t y, uint16_t color)
add_star:
    la t1, skyline_star_cnt         #keep t1 pointing at skyline star count
    lh t2, 0(t1)                    #t2 = count
    li t4,max_stars
    bge t2, t4, exitas              #exit if full
    li t0, 8                        #for 8 bytes per star
    mul t0, t0, t2                  #8*count is how many bytes have been filled by stars
    la t3, skyline_stars            #address of array start
    add t0,t0,t3                    #t0 is address of [NEXT UNFILLED STAR POSITION]
    sh a0, 0(t0)                #store x
    sh a1, 2(t0)                #store y
    sh a2, 6(t0)                #store color
    #update size
    addi t2, t2, 1              
    sh t2, 0(t1)
    exitas:
        ret



# void  remove star(uint16_t x, uint16_t y)
# variables:
#   t0: pointer to array
#   t1: x of current star 
#   t2: y of current star
#   t3: counter of stars searched
#   a0: x to be found
#   a1: y to be found
remove_star:
    mv t3, x0               #t3 is a counter
    la t4, skyline_star_cnt 
    lh t4, 0(t4)        #t4 = count
    la t0, skyline_stars    #t0 points to array
    mv a2, t0               #a2 will be our iterator
    lhu t1, 0(t0)            #t1 is head->x
    lhu t2, 2(t0)            #t2 is head->y
    bne t1, a0, looprs      #if not equal, it's not skyline_stars[0] so loop through elements
    beq t2, a1, foundrs     #if x and y are equal, it is skyline_stars[0]
    looprs:
    # variables:
    #   t4: size of array
    #   t1: x of current star 
    #   t2: y of current star
    #   t3: counter of stars searched
    #   a2: pointer to current star in the array
    #   a0: x to be found
    #   a1: y to be found
        addi t3,t3,1        #count num of stars checked
        bge t3, t4, retrs          #check if we've looked at every star
        addi a2, a2, 8             #iterate a2 to next star position
        lhu t1, 0(a2)            #t1 is curr->x
        lhu t2, 2(a2)            #t2 is curr->y
        bne t1, a0, looprs      #if not equal, it's not skyline_stars[0] so loop through elements
        beq t2, a1, foundrs     #if x and y are equal, it is skyline_stars[0]
        j looprs
    foundrs:
        #at this point, a2 should hold the current address of star to be removed, t4 contains the count
        #t0 points to the start of the array
    # variables:
    #   t0: pointer to array
    #   t1: x of current star 
    #   t2: y of current star
    #   t3: address of last star in the array
    #   t4: size of the array
    #   a3: data of last star in the array, to overwrite removed star

        addi t4, t4, -1
        la t3, skyline_star_cnt
        sh t4, 0(t3)            #decrement count
        li t3, 8                #multiply t4 by 8 to get offset of last star
        mul t3,t3,t4            #offset of last star
        add t3, t3, t0          #address of last star
        ld a3, 0(t3)            #data of last star
        sd a3, 0(a2)            #store last star in current star to be removed
        sd x0, 0(t3)            #overwrite data of last star to 0
    retrs:
    ret
# struct skyline_star         [8 BYTES TOTAL]
#       uint16_t x;           (2 bytes) -- offset = 0
#       uint16_t y;           (2 bytes) -- offset = 2
#       uint8_t dia;          (1 byte)  -- offset = 4
#                             (1 byte filler)
#       uint16_t color;       (2 bytes) -- offset = 6
# void draw star(uint16_t * fbuf, const struct skyline_star * star)
# variables:
#   t0: x of star 
#   t1: y of star 
#   t2: color of star
#   t3: offset of star from (0,0), then fbuf address to be altered
draw_star:
    beqz a1, retds        #exit if star is NULL
    la t0, skyline_stars    #find stars array
    li t1, 0
    la t2, skyline_star_cnt
    lh t3, 0(t2)
    li t4, 8                #represents 8 bytes per star
    mul t4, t4, t3          #t4 = 8*(num_stars)
    add t4, t4, t0          #t4 = next available address after array
    blt a1, t0, retds       #return if given address is before the array
    bge a1, t4, retds       #return if given address is beyond the array
    found:
        lhu t0, 0(a1)        #t0 = x
        lhu t1, 2(a1)        #t1 = y
        lh t2, 6(a1)        #t2 = color
        li t3, fbuf_width   #vertical offset
        mul t3,t3,t1        #t3 = y*640
        add t3, t3, t0      #t3 = y*640+x
        slli t3,t3,1        #t3 = (y*640+x)*2  
        add t3, a0, t3      #t3 = fbuf + offset
        sh t2, 0(t3)        #edit pixel at fbuf+offset
    retds:
        ret

# void add window (uint16_t x, uint16_t y, uint8_t w, uint8_t h, uint16_t color)
# variables
#   t4: address of head pointer 
#   t0: current head node
#   t2: address of new allocated node
add_window:
#for this function I create a new node, point it to head, then make the new node the head
#so I insert at head which is O(1)
#that way we don't have to deal with case where list is empty
    la t4, skyline_win_list
    ld t0, 0(t4)        #t0 = head
    addi sp,sp,-56
    sd a0, 0(sp)            #put arguments on stack
    sd a1, 8(sp)
    sd a2, 16(sp)
    sd a3, 24(sp)
    sd a4, 32(sp)
    sd fp, 40(sp)
    sd ra, 48(sp)            #store ra and fp
    li a0, 16         #allocate 16 bytes for a skyline window
    call malloc
    beqz a0, retaw
    mv t2,a0               #put new address into t2
    ld a0, 0(sp)            #reload original a0-a4 arguments
    ld a1, 8(sp)
    ld a2, 16(sp)
    ld a3, 24(sp)
    ld a4, 32(sp)
    ld fp, 40(sp)
    ld ra, 48(sp)            #reload original ra and fp
    addi sp, sp, 56
    #store struct variables
    sh a0, 8(t2)           #store x
    sh a1, 10(t2)           #store y
    sb a2, 12(t2)           #store w 
    sb a3, 13(t2)           #store h
    sh a4, 14(t2)          #store color
    sd t0, 0(t2)           #node->next = head
    sd t2, 0(t4)           #node = head
    ret
    retaw:
    ld a0, 0(sp)            #reload original a0-a4 arguments
    ld a1, 8(sp)
    ld a2, 16(sp)
    ld a3, 24(sp)
    ld a4, 32(sp)
    ld fp, 40(sp)
    ld ra, 48(sp)            #reload original ra and fp
    addi sp, sp, 56
    ret


# struct skyline_window                 [16 BYTES TOTAL]
#       skyline_window * next (8 bytes) -- offset = 0
#       uint16_t x;           (2 bytes) -- offset = 8
#       uint16_t y;           (2 bytes) -- offset = 10
#       uint8_t w;            (1 byte)  -- offset = 12
#       uint8_t h;            (1 byte)  -- offset = 13
#       uint16_t color;       (2 bytes) -- offset = 14
remove_window:
        la t4, skyline_win_list #t4 = skyline_win_list address
        ld t0, 0(t4)            #t0 = M[t4] = skyline_win_list
        beqz t0, exitrw           #ret if list is empty
        lhu t1, 8(t0)           #t1 = head->x
        bne t1, a0, xyloop      #go to loop if head->x != x
        lhu t1, 10(t0)           #t1 = head->y
        beq t1, a1, removehead  #remove if head->x = x and head->y = y
    xyloop:
        mv t2, t0               #t2 = prev node
        ld t0, 0(t0)            #t0 = t0->next (loop)
        beqz t0, exitrw           #return if we get to the end of the list
        lhu t1, 8(t0)           #t1 = t0->x
        bne t1, a0, xyloop      #loop if t0->x != x   
        lhu t1, 10(t0)           #t1 = t0->y 
        beq t1, a1, remove      #remove if t0->x = x and t0->y = y
        j xyloop
    removehead:
        #t0 contains the head to be removed
        ld t1, 0(t0)            #t1 = head->next
        mv a0, t0               #a0 -> head
        addi sp,sp, -16
        sd ra, 0(sp)            #store ra and fp
        sd fp, 8(sp)
        call free               #free
        ld ra, 0(sp)            #reload ra and fp
        ld fp, 8(sp)
        addi sp,sp, 16
        sd t1, 0(t4) #now head is set to the original head->next pointer
        j exitrw
    remove:
        #t0 contains the node to be removed, t2 contains the previous node
        ld t1, 0(t0)            #t1 = t0->next
        mv a0, t0               #a0 -> node to be freed
        addi sp,sp, -16
        sd ra, 0(sp)            #store ra and fp
        sd fp, 8(sp)
        call free               #free
        ld ra, 0(sp)            #reload ra and fp
        ld fp, 8(sp)
        addi sp,sp, 16
        sd t1, 0(t2)            #t2->next = t0->next 
    exitrw:
        ret

# struct skyline_window                 [16 BYTES TOTAL]
#       skyline_window * next (8 bytes) -- offset = 0
#       uint16_t x;           (2 bytes) -- offset = 8
#       uint16_t y;           (2 bytes) -- offset = 10
#       uint8_t w;            (1 byte)  -- offset = 12
#       uint8_t h;            (1 byte)  -- offset = 13
#       uint16_t color;       (2 bytes) -- offset = 14
# void draw window(uint16_t * fbuf, const struct skyline_window * win)
draw_window:
    addi sp,sp,-32         #allocate space to store saved register
    sd s4,0(sp)
    sd s1,8(sp)
    sd s2,16(sp)
    sd s3,24(sp)
    beqz a1, exitdw        #exit if window is NULL
    lh t0, 14(a1)          #t0 holds the color data
    lhu t1, 10(a1)           #t1 = win->y
    lhu t2, 8(a1)            #t2 = win->x
    lb t4, 12(a1)           #t4 = win->w
    lb t5, 13(a1)           #t5 = win->h
    mv t6, x0               #t6 will be the i counter
    #loop variables:
    #  a2: j counter (vertical iterator)
    #  t6: i counter (horizontal iterator)
    #  t5: height of window
    #  t4: width of window
    #  t2: x coordinate of window
    #  t1: y coordinate of window
    #  t0: color of window
    #  s2: offset of pixel from (0,0)
    #  s3: address in fbuf to be edited
    #  a0: fbuf address
    loopout: #(for t6 = 0; t6 < t5 && t6+t1 < 480; t6++)
        mv a2, x0           #a2 is the j counter
        bge t6, t5, exitdw  #exit if we've reached the end
        add s1, t6, t1      #s1 is the y value of the current pixel (y+i)
        li t3, fbuf_height
        bge s1, t3, exitdw  #exit if out of bounds
        loopin: #(for a2 = 0; a2 < t4 && a2+t2 < 640; a2++)
            add s4,a2,t2    #s4 is the x value of the current pixel (x+j)
            li t3, fbuf_width      # width of fbuf window
            bge s4, t3, loopoutend  #check x value against width to make sure it's in bounds

            #calculates offset from fbuf, using formula from MP1 doc

            li t3, fbuf_width              #using formula from MP1 document
            mul s2, t3, s1          #s2 = y*640
            add s2, s2, s4          #s2 = y*640+x
            slli s2,s2,1

            add s3, s2, a0          #s3 has the fbuf address we're modifying
            sh t0, 0(s3)            #edit pixel

            addi a2,a2,1                #a2++
            bge a2, t4, loopoutend         #exit inner loop if a2 > w
            j loopin                    #loop if valid
        loopoutend:
            addi t6,t6,1
            j loopout
    exitdw:
        ld s4,0(sp)
        ld s1,8(sp)
        ld s2,16(sp)
        ld s3,24(sp)
        addi sp,sp,32         #de-allocate space to store saved register
        ret

start_beacon:

        la t0, skyline_beacon             # Load address of skyline_beacon into t0 (t0 is 64-bit)

        # Store the function arguments into the struct fields
        sd a0, 0(t0)                      # Store img (a0, 64-bit) at offset 0 (8 bytes)

        sh a1, 8(t0)                      # Store x (a1, 16-bit) at offset 8 (after img pointer)

        sh a2, 10(t0)                     # Store y (a2, 16-bit) at offset 10

        sb a3, 12(t0)                     # Store dia (a3, 8-bit) at offset 12

        sh a4, 14(t0)                     # Store period (a4, 16-bit) at offset 14

        sh a5, 16(t0)                     # Store ontime (a5, 16-bit) at offset 16

        ret                               # Return to caller

# void draw beacon (uint16_t * fbuf, uint64_t t, const struct skyline_beacon * bcn)
draw_beacon:
    ld a3, 0(a2)        #a3 will hold img address
    lh a4, 8(a2)        #a4 holds x
    lh a5, 10(a2)       #a5 holds y
    lb a6, 12(a2)       #a6 holds width and height
    mv t0, a1           #move time to t0 
    lh t1, 14(a2)       #hold period in t1
    lh t2, 16(a2)       #hold ontime in t2
    rem t0,t0,t1        #t0 = t0%t1
    bge t0,t2,drawblack #return if it's not supposed to be on
    #once we get to this point, we know to draw img
    #store saved registers 
    #we can also now use t0-t2 since we already dealt with time stuff
    addi sp,sp,-32
    sd s1,0(sp)
    sd s2,8(sp)
    sd s3,16(sp)
    sd s4,24(sp)
    li t3,0         #i counter
    outloop:
        bge t3,a6,outloopend    #finish loop if finished iterating
        li t4, 0    #j counter
        add s3,t3,a5    #s3 = y+i : y of current pixel to be edited in fbuf
        li s4,fbuf_height       #depth of boundary
        bge s3,s4,outloopend    #exit if below boundary
        inloop:
            bge t4,a6, inloopend    #exit loop if j > width
            add s1,a4,t4    #s1 = x+j : x of current pixel to be edited
            li s2, fbuf_width      #check x+j versus width of boundary
            bge s1,s2,inloopend    #exit if x+j > 640 

            #get offset from img (j+i*width)*2
            mul s4,a6,t3    #s4 = i*width
            add s4,s4,t4    #s4 = j+i*width
            slli s4,s4,1    #multiply by 2
                            #s4 = (j+i*width)*2
            add s4,s4,a3    #s4 = img + offset
            lh s4,0(s4)     #s4 contains color data for img + offset

            #get offset from fbuf
            li t0,fbuf_width       #multiply by 640
            mul t0,t0,s3    #t0 = 640*y
            add t0,t0,s1    #t0 = 640*y+j
            slli t0,t0,1    #multiply by 2
                            #t0 = (640*y+x)*2
            add t0,t0,a0    #t0 = fbuf + offset to store data
            sh s4,0(t0)     #store img data into fbuf
            
            addi t4,t4,1    #increment j
            j inloop
        inloopend:
            addi t3,t3,1    #increment i
            j outloop
        drawblack:
        #once we get to this point, we know to draw img
        #store saved registers 
        #we can also now use t0-t2 since we already dealt with time stuff
        addi sp,sp,-32
        sd s1,0(sp)
        sd s2,8(sp)
        sd s3,16(sp)
        sd s4,24(sp)
        li t3,0         #i counter
        outdb:
            bge t3,a6,outloopend    #finish loop if finished iterating
            li t4, 0    #j counter
            add s3,t3,a5    #s3 = y+i : y of current pixel to be edited in fbuf
            li s4,fbuf_height       #depth of boundary
            bge s3,s4,outloopend    #exit if below boundary
            indb:
                bge t4,a6, indbend    #exit loop if j > width
                add s1,a4,t4    #s1 = x+j : x of current pixel to be edited
                li s2, fbuf_width      #check x+j versus width of boundary
                bge s1,s2,indbend    #exit if x+j > 640 

                #get offset from fbuf
                li t0,fbuf_width       #multiply by 640
                mul t0,t0,s3    #t0 = 640*y
                add t0,t0,s1    #t0 = 640*y+j
                slli t0,t0,1    #multiply by 2
                                #t0 = (640*y+x)*2
                add t0,t0,a0    #t0 = fbuf + offset to store data
                li s4, 0
                sh s4,0(t0)     #store zero data into fbuf
                
                addi t4,t4,1    #increment j
                j indb
            indbend:
                addi t3,t3,1    #increment i
                j outdb
        outloopend:
        ld s1,0(sp)
        ld s2,8(sp)
        ld s3,16(sp)
        ld s4,24(sp)
        addi sp,sp,32

    retdb:
        ret

# struct skyline_beacon       [16 BYTES TOTAL]
#       const uint16_t * img  (8 bytes) -- offset = 0
#       uint16_t x;           (2 bytes) -- offset = 8
#       uint16_t y;           (2 bytes) -- offset = 10
#       uint8_t dia;          (1 byte)  -- offset = 12
#                             (1 byte filler)  
#       uint16_t period;      (2 bytes) -- offset = 14
#       uint16_t ontime;      (2 bytes) -- offset = 16
.end