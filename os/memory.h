#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

void init_memory(void);
uint32_t kmalloc_a(uint32_t sz);  // page aligned
uint32_t kmalloc_p(uint32_t sz, uint32_t *phys); // returns physical address
uint32_t kmalloc_ap(uint32_t sz, uint32_t *phys); // page aligned and returns physical address
uint32_t kmalloc(uint32_t sz); // normal

#endif
