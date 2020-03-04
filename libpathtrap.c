/*
 * Wrapper for open, read, and close libc functions.
 *
 * FIXME add license
 *
 * FIXME for each syscall we need to check whether the fd is ours, so we have to
 * repeat the same code in every function. To avoid this we can simply use
 * something similar to https://github.com/pmem/syscall_intercept. It mentions
 * some limitations so we need to check that it works.
 *
 * FIXME cleanup includes
 */
#define _GNU_SOURCE
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <assert.h>
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <search.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/param.h>
#include <stdbool.h>

#include "libpathtrap.h"

static void *open_fds = NULL;

static int compar(const void *a, const void *b) {
	return ((struct fake_fd*)a)->fd - ((struct fake_fd*)b)->fd;
}

static struct fake_fd** lookup(int fd) {
	struct fake_fd fake_fd = {.fd = fd};
	return tfind(&fake_fd, &open_fds, compar);
}

static bool should_trap(const char *pathname) {
	if (ops.should_trap)
		return ops.should_trap(pathname);
	return false;
}

int open_fake(const char *pathname, int flags, void *priv) {
	int fd;
	struct fake_fd *fake_fd;

	if (!(fake_fd = malloc(sizeof *fake_fd)))
		return -1;
	if (asprintf(&fake_fd->pathname, "[%s]", pathname) == -1) {
		free(fake_fd);
		return -1;
	}
	if ((fd = ops.open(fake_fd, pathname, flags, priv)) == -1) {
		assert(priv);
		free(fake_fd);
		return fd;
	}
	fake_fd->fd = fd;
	tsearch(fake_fd, &open_fds, &compar);
	return fd;
}

int open(const char *pathname, int flags, ...) {
	if (!ops.open || !should_trap(pathname))
		return __real_open(pathname, flags);
	return open_fake(pathname, flags, NULL);
}

/*
 * FIXME open64 is supposed to pass O_LARGEFILE but it seems it gets translated
 * to a directory flag.
 */
int open64(const char *pathname, int flags, ...) {
	/* FIXME O_LARGEFILE == 0200000? */
	return open(pathname, flags);
}

int close(int fd) {
	if (ops.close) {
		struct fake_fd **fake_fd = lookup(fd);
		if (fake_fd) {
			int ret = ops.close(*fake_fd);
			if (ret == -1)
				return ret;
			tdelete(fake_fd, &open_fds, compar);
			free(*fake_fd);
			return ret;
		}
	}
	return __real_close(fd);
}

int __xstat(int __ver, const char *__filename, struct stat *__stat_buf) {
	/*
	 * FIXME for some reason we get a SEGFAULT if we initialize
	 * __real___xstat in ctor
	 */
	int (*__real___xstat)(int, const char*, struct stat*) = dlsym(RTLD_NEXT, "__xstat");
	if (!ops.__xstat || !should_trap(__filename))
		return __real___xstat(__ver, __filename, __stat_buf);
	return ops.__xstat(__ver, __filename, __stat_buf);
}

ssize_t _read(int fd, void *buf, size_t count, off_t *offset) {
	if (ops.read) {
		struct fake_fd **fake_fd = lookup(fd);
		if (fake_fd)
			return ops.read(*fake_fd, buf, count, offset);
	}
	if (offset)
		return __real_pread(fd, buf, count, *offset);
	return __real_read(fd, buf, count);
}

ssize_t read(int fd, void *buf, size_t count) {
	return _read(fd, buf, count, NULL);
}

size_t __read_chk(int fd, void * buf, size_t nbytes, size_t buflen __attribute__((unused))) {
	return read(fd, buf, nbytes);
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
	return _read(fd, buf, count, &offset);
}

ssize_t pread64(int fd, void *buf, size_t count, off_t offset) {
	return pread(fd, buf, count, offset);
}

size_t __pread64_chk(int fd, void * buf, size_t nbytes, off64_t offset, size_t buflen __attribute__((unused))) {
	return pread64(fd, buf, nbytes, offset);
}

ssize_t __pread_chk(int fd, void * buf, size_t nbytes, off_t offset, size_t buflen __attribute__((unused))) {
	return pread(fd, buf, nbytes, offset);
}

ssize_t _write(int fd, const void *buf, size_t count, off_t *offset) {
	if (ops.write) {
		struct fake_fd **fake_fd = lookup(fd);
		if (fake_fd)
			return ops.write(*fake_fd, buf, count, offset);
	}
	if (offset)
		return __real_pwrite(fd, buf, count, *offset);
	return __real_write(fd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count) {
	return _write(fd, buf, count, NULL);
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
	return _write(fd, buf, count, &offset);
}

ssize_t pwrite64(int fd, const void *buf, size_t count, off_t offset) {
	return _write(fd, buf, count, &offset);
}

int __xstat64(int __ver, const char *__filename, struct stat64 *__stat_buf) {
	if (!ops.__xstat64 || !should_trap(__filename))
		return __real___xstat64(__ver, __filename, __stat_buf);
	return ops.__xstat64(__ver, __filename, __stat_buf);
}

int __lxstat64 (int __ver, const char *__filename, struct stat64 *__stat_buf) {
	if (!ops.__lxstat64 || !should_trap(__filename))
		return __real___lxstat64(__ver, __filename, __stat_buf);
	return ops.__lxstat64(__ver, __filename, __stat_buf);
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
	if (!ops.readlink || !should_trap(pathname))
		return __real_readlink(pathname, buf, bufsiz);
	return ops.readlink(pathname, buf, bufsiz);
}

int ioctl(int fd, unsigned int cmd, unsigned long args) {

	if (ops.ioctl) {
		struct fake_fd **fake_fd = lookup(fd);
		if (fake_fd)
			return ops.ioctl(*fake_fd, cmd, args);
	}
	return __real_ioctl(fd, cmd, args);
}

char *realpath(const char *path, char *resolved_path) {
	if (!ops.realpath || !should_trap(path))
		return __real_realpath(path, resolved_path);
	return ops.realpath(path, resolved_path);
}

void* mmap64(void *addr, size_t length, int prot, int flags, int fd, off64_t offset) {
	if (ops.mmap64 && fd != -1) {
		struct fake_fd **fake_fd = lookup(fd);
		if (fake_fd)
			return ops.mmap64(addr, length, prot, flags, *fake_fd, offset);
	}
	assert(__real_mmap64);
	return __real_mmap64(addr, length, prot, flags, fd, offset);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
	return mmap64(addr, length, prot, flags, fd, offset);
}

__attribute__((constructor)) static void ctor()
{
	/* FIXME don't assert */
	__real_open = dlsym(RTLD_NEXT, "open");
	assert(__real_open);
	__real_close = dlsym(RTLD_NEXT, "close");
	assert(__real_close);
	__real_read = dlsym(RTLD_NEXT, "read");
	assert(__real_read);
	__real_pread = dlsym(RTLD_NEXT, "pread");
	assert(__real_pread);
	__real_write = dlsym(RTLD_NEXT, "write");
	assert(__real_write);
	__real_pwrite = dlsym(RTLD_NEXT, "pwrite");
	assert(__real_pwrite);
	__real_realpath = dlsym(RTLD_NEXT, "realpath");
	assert(__real_realpath);
	__real_ioctl = dlsym(RTLD_NEXT, "ioctl");
	assert(__real_ioctl);
	__real___xstat64 = dlsym(RTLD_NEXT, "__xstat64");
	assert(__real___xstat64);
	__real___lxstat64 = dlsym(RTLD_NEXT, "__lxstat64");
	assert(__real___lxstat64);
	__real_readlink = dlsym(RTLD_NEXT, "readlink");
	assert(__real_readlink);
	__real_mmap64 = dlsym(RTLD_NEXT, "mmap64");
}
