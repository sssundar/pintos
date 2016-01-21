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

/* Circular buffer of scan codes (or command responses) */
static volatile uint8_t kbuf[KEYBUFLEN];

/* Index to the current head in the circular buffers. */
static volatile int start;

/* Index to the current tail in the circular buffers. */
static volatile int end;

/**
 * Enqueues the given scan code in the circular buffer.
 * Only called in side an interrupt handler. No race conditions.
 *
 * @param scode One byte of a scan code.
 */
void enqueue(uint8_t scode) {
  
  kbuf[end] = scode;
  // Full queue case: overwrite the item that was enqueued longest ago by
  // writing over the element at "start". Increase "end" and "start" by 1
  // modulo n.
  if ((end + 1) % KEYBUFLEN == start) {
    start = (start + 1) % KEYBUFLEN;
  }
  end = (end + 1) % KEYBUFLEN;

}

/**
 * Dequeues a scan code from the circular buffer.
 *
 * @return The scan code from the tail of the circular buffer or zero if the
 * buffer is empty.
 */
inline uint8_t dequeue() {
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
  // Enable interrupts  
  enable_interrupts();
  return rtn;
}

// This call cannot be interrupted.
// Keyboard for QEMU is Scan Set 1 by default.
void init_keyboard(void) {  
  // Reset Queue Tail/Head Indices for command and key buffers
  start = 0;
  end = 0;

  install_interrupt_handler(KEYBOARD_INTERRUPT, irq1_handler);  
}

void keyboard_handler(void) {		    
  enqueue((uint8_t) inb(KEYBOARD_PORT));  
}

uint8_t getch(int flag) {
  // if the flag is 1, keep dequeueing until recognize
  // code 'f' or 'q', otherwise keep looping.
  // if flag is 0, return 0 when reach end of queue.
  // f scan code 2: 0x2B
  // q scan code 2: 0x15

  uint8_t scan_code;

  while(1){    
    scan_code = dequeue();

    if (scan_code == 0x21) {
      return 'f';
    }

    if (scan_code == 0x10) {
      return 'q';
    }

    if ( (flag == 0) && (scan_code == 0) ) {
      return 0;
    }  
  }

  return 0;
}

