#ifndef KEYBOARD_H
#define KEYBOARD_H

void init_keyboard(void);
void keyboard_handler(void);
void setup_keyboard_queue(void);
/**
 * Enqueues the given scan code in the circular buffer.
 *
 * @param scode One byte of a scan code.
 */
void enqueue(uint8_t scode);

/**
 * Dequeues a scan code from the circular buffer.
 *
 * @return The scan code from the tail of the circular buffer or zero if the
 * buffer is empty.
 */
uint8_t dequeue();

uint8_t getch(int flag);

#endif /* KEYBOARD_H */

