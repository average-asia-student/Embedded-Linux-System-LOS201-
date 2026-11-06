/*
 * Purpose: Shared ioctl definitions for the SMS sensor interface.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#ifndef SMS_SENSOR_H
#define SMS_SENSOR_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

#define SMS_SENSOR_IOC_MAGIC 'S'
#define SMS_SET_INTERVAL _IOW(SMS_SENSOR_IOC_MAGIC, 0x01, unsigned int)

/* Sensor readings are transferred as CSV; define any in-memory data model
 * alongside the userspace application when its interface is implemented.
 */

#endif /* SMS_SENSOR_H */
