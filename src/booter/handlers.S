.code32
.align 8

.globl irq1_handler
# Keyboard Handler
irq1_handler: 
       pusha   # Save registers from the interrupted code!
       cld     # If calling C code, clear direction flag

       # set up call frame 
       push %ebp
       mov %esp, %ebp

       call keyboard_handler

       # return old call frame
       pop %ebp         

       # This interrupt only came from PIC-master, so we don't have to send
       # an EOI to the slave.
       mov     $0x20, %al
       out     %al, $0x20
       # why not
       mov     $0xa0, %al
       out     %al, $0x20

       popa    # Restore all registers
       iret    # Go back to interrupted code in 32 bit protected mode

.globl irq0_handler
# Timer Handler
irq0_handler:	
       pusha   # Save registers from the interrupted code!
       cld     # If calling C code, clear direction flag

       # set up call frame 
       push %ebp
       mov %esp, %ebp
       	
       call timer_handler

       # return old call frame
       pop %ebp	   

       # This interrupt only came from PIC-master, so we don't have to send
       # an EOI to the slave.
       mov     $0x20, %al
       out     %al, $0x20
       # why not
       mov     $0xa0, %al
       out     %al, $0x20

       popa    # Restore all registers
       iret    # Go back to interrupted code in 32 bit protected mode
