#include "console.h"
#include "skyline.h"
#include "string.h"

struct skyline_star skyline_stars[SKYLINE_STARS_MAX];
uint16_t skyline_star_cnt;
struct skyline_beacon skyline_beacon;
struct skyline_window * skyline_win_list;

void main(void){
    add_window(100,100,4,4,(uint16_t)(0x0999));
    add_window(200,200,5,5,(uint16_t)(0x9900));
    add_window(150,50,10,10,(uint16_t)(0xb00b));
    struct skyline_window*win = skyline_win_list;
    console_printf("before:\n\n");
    while(win!=NULL){
        
        console_printf("addr: 0x%x;\n x: 0x%x;\n y: 0x%x;\n w: %x;\n h: %x;\n color: 0x%x\n", win,win->x, win->y, win->w, win->h, win->color);
        win = win->next;
    }
    remove_window(100,100);
    remove_window(150,50);
    remove_window(100,50);
    add_window(100,100,5,4,(uint16_t)(0x0999));
    remove_window(100,100);
    add_window(300,300,6,6,(uint16_t)(0x8888));
    win = skyline_win_list;
    console_printf("\n\nafter:\n\n");
    while(win!=NULL){
        
        console_printf("addr: 0x%x;\n x: 0x%x;\n y: 0x%x;\n w: %x;\n h: %x;\n color: 0x%x\n", win ,win->x, win->y, win->w, win->h, win->color);
        win = win->next;
    }
}