//
//  linux_bitrev.h
//  blind-nand-dumper
//
//  Created by tihmstar on 10.10.24.
//

#ifndef linux_bitrev_h
#define linux_bitrev_h

#include <stdint.h>

extern uint8_t const byte_rev_table[256];
static inline uint8_t bitrev8(uint8_t byte){
    return byte_rev_table[byte];
}

#endif /* linux_bitrev_h */
