#include "isr.h"
#include "vga.h"

isr_t interrupt_handlers[256];

void register_interrupt_handler(uint8_t n, isr_t handler) {
    interrupt_handlers[n] = handler;
}

// This gets called from our ASM interrupt handler stub.
void isr_handler(registers_t regs) {
    vga_writestring("Received interrupt: ");

    // Convert int_no to string and print (simplified for bare metal)
    char int_str[4];
    int i = 0;
    uint32_t num = regs.int_no;

    if (num == 0) {
        vga_writestring("0");
    } else {
        while(num > 0) {
            int_str[i++] = (num % 10) + '0';
            num /= 10;
        }
        for(int j = i - 1; j >= 0; j--) {
            vga_putchar(int_str[j]);
        }
    }
    vga_writestring("\n");

    if (interrupt_handlers[regs.int_no] != 0) {
        isr_t handler = interrupt_handlers[regs.int_no];
        handler(&regs);
    }
}
