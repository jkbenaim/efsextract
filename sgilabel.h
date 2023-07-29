#pragma once

#include <stdint.h>

struct sgilabel_s {
        uint32_t magic1;        /* 0x00072959 */
        uint8_t  pad1[90];
        char name[50];
        char serial[50];
        uint8_t pas2[84];       /* 0x00121053 */
        uint32_t magic2;
        char fxid[64];
} __attribute__((pakced));
