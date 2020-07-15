/*
 * Copyright (c) 2020 Nutanix Inc. All rights reserved.
 *
 * Authors: Thanos Makatos <thanos@nutanix.com>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *      * Neither the name of Nutanix nor the names of its contributors may be
 *        used to endorse or promote products derived from this software without
 *        specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *  DAMAGE.
 *
 */

#ifndef __LIBPATHTRAP_H__
#define __LIBPATHTRAP_H__

#define _GNU_SOURCE
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

struct fake_fd {
	int fd;
	char *pathname;
	int offset;
	void *priv;
};

static inline void *fake_fd_get_priv(struct fake_fd *fake_fd) {
	return fake_fd->priv;
}

static inline void fake_fd_set_priv(struct fake_fd *fake_fd, void *data) {
	fake_fd->priv = data;
}

extern struct ops {
	bool (*should_trap)(const char*);
	int (*open)(struct fake_fd*, const char*, int, void*);
	int (*close)(struct fake_fd*);
	ssize_t (*read)(struct fake_fd*, void*, size_t, off_t*);
	ssize_t (*write)(struct fake_fd*, const void*, size_t, off_t*);
	int (*__xstat)(int, const char*, struct stat*);
	int (*__xstat64)(int, const char*, struct stat64*);
	int (*ioctl)(struct fake_fd*, unsigned int, unsigned long);
	char* (*realpath)(const char*, char*);
	ssize_t (*readlink)(const char*, char*, size_t);
	int (*__lxstat64)(int, const char*, struct stat64*);
	void* (*mmap64)(void*, size_t, int, int, struct fake_fd*, off64_t);
} ops;

extern int (*__real_open)(const char*, int);
extern int (*__real_read)(int, void*, size_t);
extern int (*__real_pread)(int, void*, size_t, off_t);
extern ssize_t (*__real_write)(int, const void*, size_t);
extern ssize_t (*__real_pwrite)(int, const void*, size_t, off_t);
extern char* (*__real_realpath)(const char*, char*);
extern int (*__real_ioctl)(int, unsigned int, unsigned long);
extern int (*__real_close)(int);
extern int (*__real___xstat)(int, const char*, struct stat*);
extern int (*__real___fxstat64)(int, int, struct stat64*);
extern int (*__real___xstat64)(int, const char*, struct stat64*);
extern int (*__real___lxstat64)(int, const char*, struct stat64*);
extern ssize_t (*__real_readlink)(const char*, char*, size_t);
extern void* (*__real_mmap64)(void*, size_t, int, int, int, off64_t);

int open_fake(const char*, int, void*);

#endif /* __LIBPATHTRAP_H__ */
