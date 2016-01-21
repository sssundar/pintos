#include <stdint.h>
#include "ports.h"
#include "handlers.h"
#include "interrupts.h"

/**
 * This is the IO port of the PS/2 controller, where the keyboard's scan
 * codes are made available.  Scan codes can be read as follows:
 *
 *     unsigned char scan_code = inb(KEYBOARD_PORT);
 *
 * Most keys generate a scan-code when they are pressed, and a second scan-
 * code when the same key is released.  For such keys, the only difference
 * between the "pressed" and "released" scan-codes is that the top bit is
 * cleared in the "pressed" scan-code, and it is set in the "released" scan-
 * code.
 *
 * A few keys generate two scan-codes when they are pressed, and then two
 * more scan-codes when they are released.  For example, the arrow keys (the
 * ones that aren't part of the numeric keypad) will usually generate two
 * scan-codes for press or release.  In these cases, the keyboard controller
 * fires two interrupts, so you don't have to do anything special - the
 * interrupt handler will receive each byte in a separate invocation of the
 * handler.
 *
 * See http://wiki.osdev.org/PS/2_Keyboard for details.
 */
#define KEYBOARD_PORT 	0x60

/** Length of the circular keyboard buffer (needs to be less than 8 bits) */
#define KEYBUFLEN 		100

/* TODO:  You can create static variables here to hold keyboard state.
 *        Note that if you create some kind of circular queue (a very good
 *        idea, you should declare it "volatile" so that the compiler knows
 *        that it can be changed by exceptional control flow.
 *
 *        Also, don't forget that interrupts can interrupt *any* code,
 *        including code that fetches key data!  If you are manipulating a
 *        shared data structure that is also manipulated from an interrupt
 *        handler, you might want to disable interrupts while you access it,
 *        so that nothing gets mangled...
 */

/* Circular buffer of scan codes */
static volatile uint8_t kbuf[KEYBUFLEN];

/* Index to the current head in the circular scan-code buffer. */
static volatile int start;

/* Index to the current tail in the circular scan-code buffer. */
static volatile int end;

/**
 * Enqueues the given scan code in the circular buffer.
 *
 * @param scode One byte of a scan code.
 */
void enqueue(uint8_t scode) {
  // Disable interrupts: don't save return value as we ALWAYS have them enabled
  disable_interrupts();
  
  kbuf[end] = scode;
  // Full queue case: overwrite the item that was enqueued longest ago by
  // writing over the element at "start". Increase "end" and "start" by 1
  // modulo n.
  if ((end + 1) % KEYBUFLEN == start) {
    start = (start + 1) % KEYBUFLEN;
  }
  end = (end + 1) % KEYBUFLEN;

  // Enable interrupts:
  enable_interrupts();
}

/**
 * Dequeues a scan code from the circular buffer.
 *
 * @return The scan code from the tail of the circular buffer or zero if the
 * buffer is empty.
 */
uint8_t dequeue() {
  // Disable interrupts: don't save return value as we ALWAYS have them enabled
  disable_interrupts();

  uint8_t rtn;
  // Empty queue case:
  if (start == end) {
    rtn = 0;
  }
  // Non-empty queue case:
  else {
    rtn = kbuf[start];
    kbuf[start] = 0;
    start = (start + 1) % KEYBUFLEN;
  }
  return rtn;

  // Enable interrupts: don't have to 
  enable_interrupts();
}


/* 
Initialize Keyboard - ask it to reset
*/

// Interrupts Must Be Disabled During This Call
void setup_keyboard_queue(void) {
  // Queue Tail/Head Indices
  start = 0;
  end = 0;
}

// This call can be interrupted.
void init_keyboard(void) {
  // Set up queue as command queue (put all our commands in it)
  // We 
  // Disable Scanning
  // Reset + Self Test
  // Set up to use scan code set 1
  // Enable Scanning

  // Only handle F (0x21, pressed), Q (0x10, pressed)
  // These are unique to this scan code.
  install_interrupt_handler(1, irq1_handler);
}

void keyboard_handler(void) {		
}

uint8_t getch(int flag) {
  // if the flag is 1, keep dequeueing until recognize
  // code 'f' or 'q', otherwise keep looping.
  // if flag is 0, return 0 when reach end of queue.
  // f scan code 2: 0x2B
  // q scan code 2: 0x15

  uint8_t scan_code;

  while(1){
    if (flag != 0 && flag != 1){
      fprintf("Error: Received invalid getch flag.");
      exit(1);
    }
    else{
      scan_code = dequeue();

      if(scan_code == 0x2B){
	return 'f';
      }
      if(scan_code == 0x15){
	return 'q';
      }

      if(flag == 0 && scan_code == 0){
	return 0;
      }
    }
  }
}

