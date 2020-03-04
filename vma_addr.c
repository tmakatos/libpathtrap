/*
 * Copyright (c) 2020 Nutanix Inc. All rights reserved.
 *
 * Author: Felipe Franciosi <felipe@nutanix.com>
 *
 */

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "vma_addr.h"

#define PROC_SELF_MAPS  "/proc/self/maps"
#define PROC_SELF_FD    "/proc/self/fd"

static int
fd_find(uint64_t inode)
{
    DIR *dp;
    struct dirent *de;
    int fd = -1;
    int err;

    assert(inode != 0);

    dp = opendir(PROC_SELF_FD);
    if (dp == NULL) {
        return -1;
    }

    while ((de = readdir(dp)) != NULL) {
        char path[PATH_MAX];
        char real[PATH_MAX];
        struct stat st;

        if (de->d_type != DT_LNK) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", PROC_SELF_FD, de->d_name);

        memset(real, 0, sizeof(real));
        err = readlink(path, real, PATH_MAX - 1);
        if (err == -1) {
            continue;
        }

        err = stat(real, &st);
        if (err == -1) {
            continue;
        }

        if (st.st_ino == inode) {
            fd = atoi(de->d_name);
            break;
        }
    }

    (void)closedir(dp);

    return fd;
}

static int
vma_find(void *addr, vma_info_t *vma_info)
{
    FILE *proc_maps_fp;
    char *line = NULL;
    size_t len = 0;
    int err;
    int ret = -1;

    assert(vma_info != NULL);

    proc_maps_fp = fopen(PROC_SELF_MAPS, "r");
    if (proc_maps_fp == NULL) {
        return -1;
    }

    while (getline(&line, &len, proc_maps_fp) != -1) {
        void *vma_start, *vma_end;
        char prot_r, prot_w, prot_x, map_pvt;
        unsigned long long offset;
        uint8_t major, minor;
        uint64_t inode;
        char path[PATH_MAX];

        err = sscanf(line,
                     "%p-%p %c%c%c%c %llx %hhx:%hhx %"PRIu64" %s ",
                     &vma_start, &vma_end,
                     &prot_r, &prot_w, &prot_x, &map_pvt,
                     &offset,
                     &major, &minor,
                     &inode,
                     path);
        if (err != 10 && err != 11) {
            fprintf(stderr, "XXX err=%d line='%s'\n", err, line);
            errno = EBADF;
            goto out;
        }
        if (err == 10) {
            memset(&path, 0, sizeof(path));
        }

        if (addr >= vma_start && addr < vma_end) {
            vma_info->vma_start = vma_start;
            vma_info->vma_end = vma_end;
            vma_info->vma_len = vma_end - vma_start;
            vma_info->prot_r = (prot_r == 'r') ? true : false;
            vma_info->prot_w = (prot_w == 'w') ? true : false;
            vma_info->prot_x = (prot_x == 'x') ? true : false;
            vma_info->map_pvt = (map_pvt == 'p') ? true : false;
            vma_info->major = major;
            vma_info->minor = minor;
            vma_info->inode = inode;
            strcpy(vma_info->path, path);
            if (inode != 0) {
                vma_info->fd = fd_find(inode);
            }

            ret = 0;

            goto out;
        }
    }

    errno = ENOENT;

out:
    free(line);
    (void)fclose(proc_maps_fp);
    return ret;
}

int
vma_addr(void *addr, vma_info_t *vma_info)
{
    int err;

    if (addr == NULL || vma_info == NULL) {
        errno = EINVAL;
        return -1;
    }

    err = vma_find(addr, vma_info);

    return err;
}
