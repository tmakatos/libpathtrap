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

/* FIXME add optional allocator/deallocator functions for fake_fd so that implementation can embed additional data per fake_fd without having to manually allocate memory and store in fake_fd.priv */
/* FIXME define environment variable to enable/disable logging using syslog(3), and log level */
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

int (*__real_open)(const char*, int);
int (*__real_read)(int, void*, size_t);
int (*__real_pread)(int, void*, size_t, off_t);
ssize_t (*__real_write)(int, const void*, size_t);
ssize_t (*__real_pwrite)(int, const void*, size_t, off_t);
char* (*__real_realpath)(const char*, char*);
int (*__real_ioctl)(int, unsigned int, unsigned long);
int (*__real_close)(int);
int (*__real___xstat)(int, const char*, struct stat*);
int (*__real___fxstat64)(int, int, struct stat64*);
int (*__real___xstat64)(int, const char*, struct stat64*);
int (*__real___lxstat64)(int, const char*, struct stat64*);
ssize_t (*__real_readlink)(const char*, char*, size_t);
void* (*__real_mmap64)(void*, size_t, int, int, int, off64_t);

int open_fake(const char*, int, void*);
