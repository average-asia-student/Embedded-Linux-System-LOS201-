
#ifndef ASGN1_IOCTL_H
#define ASGN1_IOCTL_H

/* Include ioctl definitions */
#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

/* Shared constants */
#define ASGN1_IOC_MAGIC 'A'
#define ASGN1_VERSION_SIZE 64

/* Driver statistics */
struct asgn1_stats {
    int open_count;
    int write_count;
    int read_count;
    int buffer_len;
    int last_write_size;
};

/* IOCTL commands */
#define ASGN1_RESET_BUFFER \
    _IO(ASGN1_IOC_MAGIC, 1)

#define ASGN1_GET_STATS \
    _IOR(ASGN1_IOC_MAGIC, 2, struct asgn1_stats)

#define ASGN1_SET_MODE \
    _IOW(ASGN1_IOC_MAGIC, 3, int)

#define ASGN1_GET_VERSION \
    _IOR(ASGN1_IOC_MAGIC, 4, char[ASGN1_VERSION_SIZE])

#endif /* ASGN1_IOCTL_H */
