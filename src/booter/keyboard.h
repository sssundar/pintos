#ifndef KEYBOARD_H
#define KEYBOARD_H

void init_keyboard(void);
void keyboard_handler(void);
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

/**
 * Get a char from the keyboard buffer, blocking if 'block' is set to
 * 1, not blocking if 'block' is set to 0.
 *
 * @param block 1 to block on input, 0 to not block on input.
 *
 * @return Char from the keyboard.
 */
uint8_t getch(int block);

#endif /* KEYBOARD_H */

