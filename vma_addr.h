/*
 * Copyright (c) 2020 Nutanix Inc. All rights reserved.
 *
 * Author: Felipe Franciosi <felipe@nutanix.com>
 *
 */

#ifndef VMA_ADDR_H
#define VMA_ADDR_H

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void *vma_start;
    void *vma_end;
    uint64_t vma_len;
    bool prot_r;
    bool prot_w;
    bool prot_x;
    bool map_pvt;
    uint8_t major;
    uint8_t minor;
    uint64_t inode;
    char path[PATH_MAX];
    int fd;
} vma_info_t;

int
vma_addr(void *addr, vma_info_t *vma_info);

#endif /* VMA_ADDR_H */
