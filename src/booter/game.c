#include "interrupts.h"
#include "video.h"
#include "timer.h"

/* This is the entry-point for the game! */
void c_start(void) {
    /* TODO:  You will need to initialize various subsystems here.  This
     *        would include the interrupt handling mechanism, and the various
     *        systems that use interrupts.  Once this is done, you can call
     *        enable_interrupts() to start interrupt handling, and go on to
     *        do whatever else you decide to do!
     */

    init_interrupts(); // Masks all interrupts, clears IDT, installs it.
	init_timer();

	// Now, let me set up a handler in assembly for the keyboard.
	// I need to point to a function (following cdecl) in keyboard.c, .h
	// and import these.
	// In the keyboard initialization (which I need to call) 
	// I need to install this interrupt handler, configure the keyboard, then
	// unmask the interrupt. 

    // The C interrupt HANDLER will just take the thing as an argument
    // and do something else to kill time, then return nothing, so I can
    // make sure interrupts work the way I expect with this device.

	// then here I need to allow interrupts.

    /* Loop forever, so that we don't fall back into the bootloader code. */    
  
  /* Loop forever, so that we don't fall back into the bootloader code. */
  set_bkg(GREEN);

  clear_screen();
  mvprintfcol(10, 10, RED, BLUE, "hello");
  refresh_screen();

  IRQ_clear_mask(0); // timer unmasked
  enable_interrupts();
  
  while (1) { }
}
