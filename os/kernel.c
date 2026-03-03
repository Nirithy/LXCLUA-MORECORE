#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vga.h"
#include "serial.h"
#include "gdt.h"
#include "idt.h"
#include "memory.h"
#include "isr.h"

void kernel_main(void) {
    // Initialize standard terminal output
    vga_init();
    vga_writestring("Booting LXCLUA-NCore OS Phase 1...\n");

    // Initialize serial for headless debugging
    if (serial_init() == 0) {
        serial_writestring("Serial initialized successfully. Booting OS Phase 1...\r\n");
    }

    // Set up Global Descriptor Table
    init_gdt();
    vga_writestring("GDT initialized.\n");

    // Set up Interrupt Descriptor Table
    init_idt();
    vga_writestring("IDT initialized.\n");

    // Initialize Memory Management
    init_memory();
    vga_writestring("Memory initialized.\n");

    vga_writestring("Bare metal foundation loaded successfully.\n");

    // Loop indefinitely
    while (1) {
        asm volatile ("hlt");
    }
}
