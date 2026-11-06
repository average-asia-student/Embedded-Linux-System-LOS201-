#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "counter"
#define PROC_NAME "counter_info"
#define INPUT_SIZE 32

static atomic_t counter_value = ATOMIC_INIT(0);
static atomic_t total_reads = ATOMIC_INIT(0);
static atomic_t total_writes = ATOMIC_INIT(0);
static struct proc_dir_entry *counter_proc_entry;

/*
 * Return the counter value as a newline-terminated decimal string, then
 * increment the counter once after a successful logical read.
 * @file: file instance being read.
 * @user_buffer: destination buffer in userspace.
 * @count: size of the userspace buffer.
 * @ppos: file position used to return EOF after the first read.
 * Return: bytes copied, 0 at EOF, or a negative error code.
 */
static ssize_t counter_read(struct file *file, char __user *user_buffer,
                            size_t count, loff_t *ppos)
{
    char output[INPUT_SIZE];
    int current_value;
    int output_size;

    if (*ppos != 0)
        return 0;

    current_value = atomic_read(&counter_value);
    output_size = scnprintf(output, sizeof(output), "%d\n", current_value);
    if (count < output_size)
        return -EINVAL;
    if (copy_to_user(user_buffer, output, output_size))
        return -EFAULT;

    *ppos += output_size;
    atomic_inc(&counter_value);
    atomic_inc(&total_reads);
    return output_size;
}

/*
 * Parse a non-negative decimal integer and set the counter to that value.
 * @file: file instance being written.
 * @user_buffer: source buffer in userspace.
 * @count: number of bytes provided by userspace.
 * @ppos: file position, unused by this device.
 * Return: input byte count on success, or a negative error code.
 */
static ssize_t counter_write(struct file *file, const char __user *user_buffer,
                             size_t count, loff_t *ppos)
{
    char input[INPUT_SIZE];
    int new_value;

    if (count == 0 || count >= sizeof(input))
        return -EINVAL;
    if (copy_from_user(input, user_buffer, count))
        return -EFAULT;
    input[count] = '\0';

    if (kstrtoint(input, 10, &new_value) || new_value < 0)
        return -EINVAL;

    atomic_set(&counter_value, new_value);
    atomic_inc(&total_writes);
    return count;
}

/*
 * Display the current counter and cumulative read/write operation totals.
 * @seq: seq_file used to produce procfs output.
 * @unused: unused sequence iterator argument.
 * Return: 0 on success.
 */
static int counter_proc_show(struct seq_file *seq, void *unused)
{
    seq_printf(seq, "counter=%d\n", atomic_read(&counter_value));
    seq_printf(seq, "read_count=%d\n", atomic_read(&total_reads));
    seq_printf(seq, "write_count=%d\n", atomic_read(&total_writes));
    return 0;
}

/*
 * Open the counter statistics proc entry.
 * @inode: proc inode being opened.
 * @file: proc file instance.
 * Return: result from single_open().
 */
static int counter_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, counter_proc_show, NULL);
}

static const struct proc_ops counter_proc_ops = {
    .proc_open = counter_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static const struct file_operations counter_fops = {
    .owner = THIS_MODULE,
    .read = counter_read,
    .write = counter_write,
    .llseek = no_llseek,
};

static struct miscdevice counter_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &counter_fops,
    .mode = 0666,
};

/*
 * Register the misc device and create its procfs statistics entry.
 * Return: 0 on success, or a negative error code.
 */
static int __init counter_driver_init(void)
{
    int result;

    result = misc_register(&counter_misc_device);
    if (result) {
        pr_err("counter_driver: misc_register failed: %d\n", result);
        return result;
    }

    counter_proc_entry = proc_create(PROC_NAME, 0444, NULL,
                                     &counter_proc_ops);
    if (!counter_proc_entry) {
        misc_deregister(&counter_misc_device);
        return -ENOMEM;
    }

    pr_info("counter_driver: registered /dev/%s (major 10, minor %d)\n",
            DEVICE_NAME, counter_misc_device.minor);
    return 0;
}

/*
 * Remove the procfs entry and deregister the misc device.
 */
static void __exit counter_driver_exit(void)
{
    proc_remove(counter_proc_entry);
    misc_deregister(&counter_misc_device);
    pr_info("counter_driver: unregistered /dev/%s\n", DEVICE_NAME);
}

module_init(counter_driver_init);
module_exit(counter_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tong Tran Dang - SE204445");
MODULE_DESCRIPTION("Atomic misc-device counter with procfs statistics");
