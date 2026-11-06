#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "asgn1_ioctl.h"

#define DEVICE_NAME "asgn1"
#define CLASS_NAME "asgn1_class"
#define ASGN1_MAJOR 241
#define BUFFER_SIZE 1024

static char device_buffer[BUFFER_SIZE];
static int buffer_len;
static int open_count;
static int write_count;
static int read_count;
static int last_write_size;
static int driver_mode = 1;
static DEFINE_MUTEX(asgn1_mutex);
static struct class *asgn1_class;
static struct device *asgn1_device;

static const char driver_version[ASGN1_VERSION_SIZE] = "asgn1_driver v1.0";
static const char kernel_prefix[] = "[KERNEL]";

/*
 * Open the device and count successful opens.
 * @inode: inode associated with the device file.
 * @file: file instance being opened.
 * Return: 0 on success.
 */
static int asgn1_open(struct inode *inode, struct file *file)
{
    mutex_lock(&asgn1_mutex);
    open_count++;
    mutex_unlock(&asgn1_mutex);
    pr_info("asgn1_driver: device opened\n");
    return 0;
}

/*
 * Release an opened device file.
 * @inode: inode associated with the device file.
 * @file: file instance being released.
 * Return: 0 on success.
 */
static int asgn1_release(struct inode *inode, struct file *file)
{
    pr_info("asgn1_driver: device closed\n");
    return 0;
}

/*
 * Read buffered data and count successful reads that return data.
 * @file: file instance being read.
 * @user_buffer: destination buffer in userspace.
 * @count: maximum number of bytes to read.
 * @offset: current position in the device buffer.
 * Return: number of bytes read, 0 at EOF, or a negative error.
 */
static ssize_t asgn1_read(struct file *file, char __user *user_buffer,
                          size_t count, loff_t *offset)
{
    size_t bytes_to_read;
    ssize_t result;

    if (count == 0)
        return 0;

    mutex_lock(&asgn1_mutex);
    if (*offset < 0) {
        result = -EINVAL;
        goto out;
    }

    if (*offset >= buffer_len) {
        result = 0;
        goto out;
    }

    bytes_to_read = min_t(size_t, count, buffer_len - *offset);
    if (copy_to_user(user_buffer, device_buffer + *offset, bytes_to_read)) {
        result = -EFAULT;
        goto out;
    }

    *offset += bytes_to_read;
    read_count++;
    result = bytes_to_read;

out:
    mutex_unlock(&asgn1_mutex);
    return result;
}

/*
 * Replace stored data, optionally prepending the kernel-mode label.
 * @file: file instance being written.
 * @user_buffer: source buffer in userspace.
 * @count: number of bytes requested by userspace.
 * @offset: current file position, unused by this device.
 * Return: number of input bytes stored, 0 for an empty write, or an error.
 */
static ssize_t asgn1_write(struct file *file, const char __user *user_buffer,
                           size_t count, loff_t *offset)
{
    char *input;
    size_t input_size;
    size_t prefix_size;
    size_t payload_size;
    size_t i;
    int current_mode;
    ssize_t result;

    if (count == 0)
        return 0;

    input_size = min_t(size_t, count, BUFFER_SIZE - 1);
    input = memdup_user(user_buffer, input_size);
    if (IS_ERR(input))
        return PTR_ERR(input);

    mutex_lock(&asgn1_mutex);
    current_mode = driver_mode;
    prefix_size = current_mode == 0 ? sizeof(kernel_prefix) - 1 : 0;
    payload_size = min_t(size_t, input_size,
                         BUFFER_SIZE - prefix_size - 1);

    for (i = 0; i < prefix_size; i++)
        device_buffer[i] = kernel_prefix[i];
    for (i = 0; i < payload_size; i++)
        device_buffer[prefix_size + i] = input[i];

    buffer_len = prefix_size + payload_size;
    device_buffer[buffer_len] = '\0';
    write_count++;
    last_write_size = payload_size;
    result = payload_size;
    mutex_unlock(&asgn1_mutex);
    kfree(input);

    pr_info("asgn1_driver: stored %zu input bytes in mode %d\n",
            payload_size, current_mode);
    return result;
}

/*
 * Handle the four assignment ioctl commands.
 * @file: file instance issuing the command.
 * @command: ioctl command number.
 * @argument: userspace argument address, when required.
 * Return: 0 on success, or a negative error code.
 */
static long asgn1_unlocked_ioctl(struct file *file, unsigned int command,
                                 unsigned long argument)
{
    struct asgn1_stats stats;
    void __user *user_argument = (void __user *)argument;
    int requested_mode;

    switch (command) {
    case ASGN1_RESET_BUFFER:
        pr_info("asgn1_driver: ASGN1_RESET_BUFFER called\n");
        mutex_lock(&asgn1_mutex);
        memset(device_buffer, 0, sizeof(device_buffer));
        buffer_len = 0;
        write_count = 0;
        read_count = 0;
        last_write_size = 0;
        mutex_unlock(&asgn1_mutex);
        return 0;

    case ASGN1_GET_STATS:
        pr_info("asgn1_driver: ASGN1_GET_STATS called\n");
        mutex_lock(&asgn1_mutex);
        stats.open_count = open_count;
        stats.write_count = write_count;
        stats.read_count = read_count;
        stats.buffer_len = buffer_len;
        stats.last_write_size = last_write_size;
        mutex_unlock(&asgn1_mutex);
        if (copy_to_user(user_argument, &stats, sizeof(stats)))
            return -EFAULT;
        return 0;

    case ASGN1_SET_MODE:
        pr_info("asgn1_driver: ASGN1_SET_MODE called\n");
        if (copy_from_user(&requested_mode, user_argument,
                           sizeof(requested_mode)))
            return -EFAULT;
        if (requested_mode != 0 && requested_mode != 1)
            return -EINVAL;
        mutex_lock(&asgn1_mutex);
        driver_mode = requested_mode;
        mutex_unlock(&asgn1_mutex);
        return 0;

    case ASGN1_GET_VERSION:
        pr_info("asgn1_driver: ASGN1_GET_VERSION called\n");
        if (copy_to_user(user_argument, driver_version,
                         sizeof(driver_version)))
            return -EFAULT;
        return 0;

    default:
        pr_info("asgn1_driver: unsupported ioctl 0x%x\n", command);
        return -ENOTTY;
    }
}

static const struct file_operations asgn1_fops = {
    .owner = THIS_MODULE,
    .open = asgn1_open,
    .release = asgn1_release,
    .read = asgn1_read,
    .write = asgn1_write,
    .llseek = default_llseek,
    .unlocked_ioctl = asgn1_unlocked_ioctl,
};

/*
 * Register the character device and create its device node.
 * Return: 0 on success, or a negative error code.
 */
static int __init asgn1_init(void)
{
    int result;

    result = register_chrdev(ASGN1_MAJOR, DEVICE_NAME, &asgn1_fops);
    if (result < 0) {
        pr_err("asgn1_driver: register_chrdev failed: %d\n", result);
        return result;
    }

    asgn1_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(asgn1_class)) {
        result = PTR_ERR(asgn1_class);
        unregister_chrdev(ASGN1_MAJOR, DEVICE_NAME);
        return result;
    }

    asgn1_device = device_create(asgn1_class, NULL,
                                 MKDEV(ASGN1_MAJOR, 0), NULL, DEVICE_NAME);
    if (IS_ERR(asgn1_device)) {
        result = PTR_ERR(asgn1_device);
        class_destroy(asgn1_class);
        unregister_chrdev(ASGN1_MAJOR, DEVICE_NAME);
        return result;
    }

    pr_info("asgn1_driver: loaded with major %d\n", ASGN1_MAJOR);
    return 0;
}

/*
 * Remove the device node, class, and registered character device.
 */
static void __exit asgn1_exit(void)
{
    device_destroy(asgn1_class, MKDEV(ASGN1_MAJOR, 0));
    class_destroy(asgn1_class);
    unregister_chrdev(ASGN1_MAJOR, DEVICE_NAME);
    pr_info("asgn1_driver: unloaded and cleaned up\n");
}

module_init(asgn1_init);
module_exit(asgn1_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tong Tran Dang - SE204445");
MODULE_DESCRIPTION("Assignment 01 ioctl character driver");
MODULE_VERSION("1.0");
