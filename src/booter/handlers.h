/* Declare the entry-points to the interrupt handler assembly-code fragments,
 * so that the C compiler will be happy.
 *
 * You will need lines like these:  void *(irqN_handler)(void)
 */

// Timer
void *(irq0_handler)(void);
// Keyboard
void *(irq1_handler)(void);
