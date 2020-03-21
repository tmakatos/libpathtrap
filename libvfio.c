#include "libpathtrap.h"

#include <stddef.h>
#include <assert.h>
#include <errno.h>
#include <linux/vfio.h>
#include <string.h>
#include <stdio.h>
#include <linux/muser.h>
#include <muser/muser.h>
#include <muser/pci.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/mman.h>

#include "vma_addr.h"

#define debug(fmt, ...)									\
	do {										\
		fprintf(stderr, "%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__);	\
	} while (0)

/* according to uuid_parse(3) */
#define UUID_STRING_LEN 36

#define MDEV_DEV_PATH_PREFIX "/sys/bus/mdev/devices/"
#define IOMMU_GROUP_DIR "/iommu_group"
#define IOMMU_GROUP "0" /* TODO only one IOMMU group for now */
#define VFIO_CONTAINER "/dev/vfio/vfio"
#define VFIO_GROUP "/dev/vfio/" IOMMU_GROUP

static int sock = -1;
static char *uuid;

enum vfio_fd_type {
	VFIO_FD_TYPE_CONTAINER,
	VFIO_FD_TYPE_GROUP,
	VFIO_FD_TYPE_DEVICE
};

struct vfio_fd {
	enum vfio_fd_type vfio_fd_type;
};

int __open(struct fake_fd *fake_fd, const char *pathname,
	   int flags __attribute__((unused)), void *priv) {

	int fd = syscall(SYS_memfd_create, pathname, 0);
	enum vfio_fd_type vfio_fd_type;

	if (fd == -1)
		return fd;

	if (!strcmp(pathname, VFIO_CONTAINER)) {
		vfio_fd_type = VFIO_FD_TYPE_CONTAINER;
		debug("container fd=%d\n", fd);
	} else if (!strcmp(pathname, VFIO_GROUP)) {
		vfio_fd_type = VFIO_FD_TYPE_GROUP;
		debug("group fd=%d\n", fd);
	} else {
		if (!priv) {
			debug("bad path %s\n", pathname);
			close(fd);
			errno = EINVAL;
			return -1;
		}
		debug("device fd=%d\n", fd);
		vfio_fd_type = (enum vfio_fd_type)priv;
	}
	fake_fd_set_priv(fake_fd, (void*)vfio_fd_type);
	return fd;	
}

int __close(struct fake_fd *fake_fd) {
	return __real_close(fake_fd->fd);
}

bool __should_trap(const char *pathname) {
	if (!uuid)
		return false;
	if (!strcmp(pathname, VFIO_CONTAINER) || !strcmp(pathname, VFIO_GROUP))
		return true;
	if (!strncmp(pathname, MDEV_DEV_PATH_PREFIX, sizeof(MDEV_DEV_PATH_PREFIX) - 1))
		return true;
	return false;
}

ssize_t __read(struct fake_fd *fake_fd __attribute__((unused)),
               void *buf, size_t count, off_t *offset) {

	struct muser_cmd muser_cmd = {
		.type = MUSER_READ,
		.rw = {
			.count = count,
			.pos = *offset
		}
	};
	size_t ret;

#if 0
	debug("R fd=%d %s %#lx-%#lx\n",
	        fake_fd->fd, fake_fd->pathname, *offset, *offset + count);
#endif

	/*
	 * FIXME need to set fd (which is the device fd) in muser_cmd
	 * so that it knows for which device it is.
	 */
	if ((ret = __real_write(sock, &muser_cmd, sizeof muser_cmd)) != sizeof muser_cmd) {
		debug("failed to send command: %m\n");
		return ret;
	}
	if ((ret = __real_read(sock, buf, count)) != count) {
		debug("failed to read data: %m\n");
		return ret;
	}
	if ((ret = __real_read(sock, &muser_cmd, sizeof muser_cmd)) != sizeof muser_cmd) {
		debug("failed to read response: %m\n");
		return ret;
	}
	if (muser_cmd.err != (int)count) {
		debug("bad response: %d\n", muser_cmd.err);
		return muser_cmd.err;
	}
	return count;
}

ssize_t __write(struct fake_fd *fake_fd  __attribute__((unused)),
                const void *buf, size_t count, off_t *offset) {

	struct muser_cmd muser_cmd = {
		.type = MUSER_WRITE,
		.rw = {
			.count = count,
			.pos = *offset
		}
	};
	int ret;

#if 0
	debug("W fd=%d %s %#lx-%#lx\n",
	        fake_fd->fd, fake_fd->pathname, *offset, *offset + count);
#endif

	/*
	 * FIXME need to set fd (which is the device fd) in muser_cmd
	 * so that it knows for which device it is.
	 */
	if ((ret = __real_write(sock, &muser_cmd, sizeof muser_cmd)) != sizeof muser_cmd) {
		debug("failed to send command (%d): %m\n", ret);
		return ret;
	}
	if ((ret = __real_write(sock, buf, count)) != (int)count) {
		debug("failed to send data (%d): %m\n", ret);
		return ret;
	}
	if ((ret = __real_read(sock, &muser_cmd, sizeof muser_cmd)) != sizeof muser_cmd) {
		debug("failed to receive response (%d): %m\n", ret);
		return ret;
	}
	if (muser_cmd.err) {
		debug("command failed: %s\n", strerror(-muser_cmd.err));
		errno = -muser_cmd.err;
		return -1;
	}
	return count;
}

static int open_sock(void) {

	int ret;
	struct sockaddr_un addr = {.sun_family = AF_UNIX};

	assert(uuid);

	ret = snprintf(addr.sun_path, sizeof addr.sun_path,
	               MUSER_SOCK_DIR "/%s", uuid);

	if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		debug("failed to open socket: %m\n");
		return sock;
	}

	if ((ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr))) == -1) {
		debug("failed to connect to %s: %m\n", addr.sun_path);
		return ret;
	}
	
	return sock;
}

static int vfio_group_get_device_fd(struct fake_fd *fake_fd __attribute__((unused)),
                                    unsigned long args __attribute__((unused))) {
	/* FIXME need to generate name based on passed fake_fd
	 * FIXME need to associate the fd we return with the passed fake_fd
	 */
	return open_fake("device_fd", 0, (void*)VFIO_FD_TYPE_DEVICE);
}

static int vfio_set_data_eventfd(struct muser_cmd *muser_cmd, int *fds, size_t size) {
	if (muser_cmd->ioctl.data.irq_set.count * sizeof(int) != size) {
		errno = EINVAL;
		return -1;
	}
	return muser_send_fds(sock, fds, muser_cmd->ioctl.data.irq_set.count);
}

static int map_dma(vma_info_t *vma_info) {
	return muser_send_fds(sock, &vma_info->fd, 1);
}

 
int __ioctl(struct fake_fd *fake_fd, unsigned int cmd, unsigned long args) {

	int ret = 0;
	struct muser_cmd muser_cmd = { 0 };
	ssize_t minsz, argsz;
	vma_info_t vma_info;

	muser_cmd.type = MUSER_IOCTL;
	muser_cmd.ioctl.vfio_cmd = cmd;

	if (sock == -1)
		if ((sock = open_sock()) == -1)
			return sock;

	if ((minsz = get_minsz(cmd)) < 0) {
		debug("bad minsz=%lu\n", minsz);
		return -EINVAL;
	}

	/*
	 * Initialize muser_cmd.
	 */
	switch (cmd) {
		case VFIO_CHECK_EXTENSION:
			muser_cmd.ioctl.data.vfio_extension = (int)args;
			break;
		case VFIO_SET_IOMMU:
			muser_cmd.ioctl.data.iommu_type = (int)args;
			break;
		case VFIO_GROUP_UNSET_CONTAINER:
			muser_cmd.ioctl.data.container_fd = (int)args;
			break;
		case VFIO_GROUP_GET_DEVICE_FD:
			muser_cmd.ioctl.data.device_fd = (int)args;
			return  vfio_group_get_device_fd(fake_fd,
					muser_cmd.ioctl.data.device_fd);
		default:
			memcpy(&muser_cmd.ioctl.data, (void*)args, minsz);
	}

	switch (cmd) {
		case VFIO_DEVICE_SET_IRQS: /* TODO this can go into switch above */
			switch (muser_cmd.ioctl.data.irq_set.flags & VFIO_IRQ_SET_DATA_TYPE_MASK) {
				case VFIO_IRQ_SET_DATA_EVENTFD:
					break;
				case VFIO_IRQ_SET_DATA_NONE:
				case VFIO_IRQ_SET_DATA_BOOL:
					debug("ignore IRQ set %#x\n",
					        muser_cmd.ioctl.data.irq_set.flags);
					return 0;
				default:
					return -EINVAL;
			}
			break;
		case VFIO_IOMMU_MAP_DMA:
			ret = vma_addr((void*)muser_cmd.ioctl.data.dma_map.vaddr, &vma_info);
			if (ret != 0 || vma_info.fd == -1 || vma_info.map_pvt) {
				debug("ignore vma for vaddr=%#llx, iova=%#llx-%#llx: %m\n",
				        muser_cmd.ioctl.data.dma_map.vaddr,
				        muser_cmd.ioctl.data.dma_map.iova,
				        muser_cmd.ioctl.data.dma_map.iova + muser_cmd.ioctl.data.dma_map.size);
				return 0;
			}
			/*
			 * FIXME abuse field to communicate file offset so that
			 * we don't need to introduce a new struct.
			 */
			muser_cmd.ioctl.data.dma_map.vaddr -= (__u64)vma_info.vma_start;
			break;
	}

	if ((ret = __real_write(sock, &muser_cmd, sizeof muser_cmd)) == -1) {
		debug("failed to send command: %m\n");
		return ret;
	}

	switch (cmd) {
		case VFIO_DEVICE_SET_IRQS:
			if ((argsz = get_argsz(cmd, &muser_cmd)) < 0)
				return argsz;

			/*
			 * FIXME we can also send the muser_cmd as part of the payload,
			 * instead of sending it separately at an earlier point. This
			 * would be ideal also for DMA_MAP as we only have to send one
			 * fd. For IRQs there doesn't seem to be a limit, so I'm not
			 * sure how it will be handled at the other end, e.g. will there
			 * have to be a maximum message size so we might have to spit
			 * it?
			 */
			ret = vfio_set_data_eventfd(&muser_cmd, (int*)(args + minsz),
			                            argsz - minsz);
			if (ret == -1)
				return ret;
			break;
		case VFIO_IOMMU_MAP_DMA:
			if ((ret = map_dma(&vma_info)) == -1) {
				debug("failed to map DMA: %m\n");
				return ret;
			}	
			break;
		case VFIO_DEVICE_GET_REGION_INFO:
			if ((argsz = get_argsz(cmd, &muser_cmd)) < 0)
				return argsz;
			if (argsz > minsz) {
				/*
				 * FIXME we must now read the response from libmuser, which will
				 * be at least sizeof(struct muser_cmd) bytes. Whatever we read
				 * before those last bytes is sparse info (can be zero). Non-sparse
				 * information must be stored in the passed struct vfio_region_info
				 * (flags/cap_offset/size/offset), which is the args argument of
				 * this function.
				 *
				 * The first time the user calls VFIO_DEVICE_GET_REGION_INFO,
				 * the struct vfio_region_info is not large enough to accomodate
				 * any sparse info. If there is sparse info, it is indicated so
				 * by libmuser by setting the argz and cap_offset fields accordingly
				 * in order to indicate the required space. Then the user calls
				 * again VFIO_DEVICE_GET_REGION_INFO with argz sufficiently large
				 * to hold the sparse information (check function
				 * dev_get_sparse_mmap_cap).
				 *
				 * Therefore if argz is larger than sizeof struct vfio_region_info,
				 * we need to read argz - sizeof struct vfio_region_info bytes
				 * which will be the sparse information, plus the response (sizeof struct muser_cmd).
				 *
				 * libmuser replies first with sparse info and then with
				 * struct vfio_region_info, however the args argument
				 * contains them in the reverse order.
				 */
				if ((ret = __real_read(sock, (void*)(args + minsz), argsz - minsz)) != argsz - minsz) {
					debug("short read: %d/%ld\n", ret, argsz - minsz);
					return -1;
				}
			}
			break;
	}
	if ((ret = __real_read(sock, &muser_cmd, sizeof muser_cmd)) != sizeof muser_cmd) {
		return ret;
	}
	if (cmd == VFIO_IOMMU_UNMAP_DMA && muser_cmd.err == -ENOENT) {
		muser_cmd.err = 0;
	}
	if (muser_cmd.err) {
		debug("VFIO command %s failed with %d\n",
                      vfio_cmd_to_str(cmd), muser_cmd.err);
		return muser_cmd.err;
	}

	switch (cmd) {
		case VFIO_GET_API_VERSION:
			return muser_cmd.ioctl.data.vfio_api_version;
		case  VFIO_CHECK_EXTENSION:
			return muser_cmd.ioctl.data.vfio_extension;
		case VFIO_SET_IOMMU:
		case VFIO_DEVICE_RESET:
			return 0;
	}

	memcpy((void*)args, &muser_cmd.ioctl.data, minsz);

	return 0;
}

int ____xstat(int ver __attribute__((unused)), const char *filename __attribute__((unused)),
              struct stat *stat_buf) {

	memset(stat_buf, 0, sizeof *stat_buf);
	stat_buf->st_mode = S_IFREG | S_IRWXU | S_IRWXG | S_IXGRP | S_IROTH | S_IXOTH;
	return 0;
}

int ____xstat64(int ver __attribute__((unused)),
                const char *filename __attribute__((unused)),
		struct stat64 *stat_buf) {

	memset(stat_buf, 0, sizeof *stat_buf);
	stat_buf->st_mode = S_IFREG | S_IRWXU | S_IRWXG | S_IXGRP | S_IROTH | S_IXOTH;
	return 0;
}

ssize_t __readlink(const char *pathname, char *buf, size_t bufsiz) {
	assert(uuid);
	if (!strncmp(pathname, MDEV_DEV_PATH_PREFIX, sizeof(MDEV_DEV_PATH_PREFIX) - 1)
	    && !strncmp(pathname + sizeof(MDEV_DEV_PATH_PREFIX) - 1, uuid, UUID_STRING_LEN)
	    && !strncmp(pathname + sizeof(MDEV_DEV_PATH_PREFIX) - 1 + UUID_STRING_LEN, IOMMU_GROUP_DIR, sizeof(IOMMU_GROUP_DIR) - 1)) {
		/*
		 * TODO I think what we actually need here is for the last
		 * component to be 0 (or a number in general).
		 */
		strncpy(buf, "/sys/kernel/iommu_groups/" IOMMU_GROUP, bufsiz);
		return strlen(buf);
	}
	errno = ENOENT;
	return -1;
}

char *__realpath(const char *path, char *resolved_path) {
	if (!resolved_path)
		resolved_path = strdup(path);
	else
		strcpy(resolved_path, path);
	return resolved_path;
}

static void *__mmap64(void *addr __attribute__((unused)), size_t length, int prot, int flags,
                      struct fake_fd *fake_fd, off_t offset) {

	int ret;
	struct muser_cmd muser_cmd = { 
		.type = MUSER_MMAP,
		.mmap.request = {
			.len = length,
			.addr = offset
		}
	};
	int fd;

	if (fake_fd->priv != (void*)VFIO_FD_TYPE_DEVICE) {
		errno = EINVAL;
		return MAP_FAILED;
	}

	if ((ret = __real_write(sock, &muser_cmd, sizeof muser_cmd)) == -1) {
		debug("failed to send command: %m\n");
		return MAP_FAILED;
	}
	ret = muser_recv_fds(sock, &fd, 1);
	if (ret != 1) {
		debug("failed to receive device memory fd (%d): %m\n", ret);
		return MAP_FAILED;
	}
	if ((ret = __real_read(sock, &muser_cmd, sizeof muser_cmd)) != sizeof muser_cmd) {
		debug("failed to receive response (%d): %m\n", ret);
		return MAP_FAILED;
	}
	if (muser_cmd.err) {
		debug("command failed: %s\n", strerror(-muser_cmd.err));
		errno = -muser_cmd.err;
		return MAP_FAILED;
	}
	return __real_mmap64(NULL, length, prot, flags, fd, offset);
}

struct ops ops = {
	.should_trap = &__should_trap,
	.open = &__open,
	.close = &__close,
	.read = &__read,
	.write = &__write,
	.ioctl = &__ioctl,
	.__xstat = &____xstat,
	.__xstat64 = &____xstat64,
	.__lxstat64 = &____xstat64,
	.readlink = &__readlink,
	.realpath = &__realpath,
	.mmap64 = &__mmap64
};

__attribute__((constructor)) static void ctor()
{
	uuid = getenv("LIBVFIO_UUID");
	if (uuid && strlen(uuid) != UUID_STRING_LEN)
		uuid = NULL;
}
