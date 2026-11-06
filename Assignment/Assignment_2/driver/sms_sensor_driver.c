/*
 * Purpose: SMS sensor character-device kernel module.
 * Author: Tong Tran Dang (SE204445)
 * Date: 2026-11-06
 */
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "sms_sensor.h"

#define SMS_DEVICE_NAME "sms_sensor"
#define SMS_CLASS_NAME "sms_sensor_class"
#define SMS_PROC_NAME "sms_stats"
#define SMS_DEFAULT_INTERVAL_MS 100U
#define SMS_LINE_SIZE 64

struct sms_sensor_device {
    dev_t devt;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct proc_dir_entry *proc_entry;

    struct mutex lock;
    u32 interval_ms;
    u64 read_count;
    s32 last_temperature_tenths;
    s32 last_humidity_tenths;
    ktime_t started_at;
    ktime_t last_read_at;
    bool has_read;
};

struct sms_sensor_file {
    struct mutex lock;
    char line[SMS_LINE_SIZE];
    size_t line_len;
    size_t line_pos;
    bool sample_pending;
};

static struct sms_sensor_device smsdev;

static int sms_sensor_open(struct inode *inode, struct file *file)
{
    struct sms_sensor_file *context;

    context = kzalloc(sizeof(*context), GFP_KERNEL);
    if (!context)
        return -ENOMEM;

    mutex_init(&context->lock);
    file->private_data = context;
    return 0;
}

static int sms_sensor_release(struct inode *inode, struct file *file)
{
    kfree(file->private_data);
    file->private_data = NULL;
    return 0;
}

static int sms_sensor_create_sample(struct sms_sensor_file *context)
{
    s32 temperature;
    s32 humidity;
    s64 elapsed_ms;
    s64 timestamp_ms;
    u32 interval_ms;
    u32 remaining_ms;
    ktime_t now;

    for (;;) {
        mutex_lock(&smsdev.lock);
        interval_ms = smsdev.interval_ms;
        now = ktime_get();

        if (smsdev.has_read) {
            elapsed_ms = ktime_to_ms(ktime_sub(now, smsdev.last_read_at));
            if (elapsed_ms < interval_ms) {
                remaining_ms = interval_ms - (u32)elapsed_ms;
                mutex_unlock(&smsdev.lock);

                if (msleep_interruptible(remaining_ms))
                    return -ERESTARTSYS;
                continue;
            }
        }

        timestamp_ms = ktime_to_ms(now);
        temperature = 150 + (s32)(get_random_u32() % 301U);
        humidity = 300 + (s32)(get_random_u32() % 601U);

        smsdev.read_count++;
        smsdev.last_temperature_tenths = temperature;
        smsdev.last_humidity_tenths = humidity;
        smsdev.last_read_at = now;
        smsdev.has_read = true;

        context->line_len = scnprintf(
            context->line, sizeof(context->line), "%lld,%d.%d,%d.%d\n",
            (long long)timestamp_ms, temperature / 10, temperature % 10,
            humidity / 10, humidity % 10);
        context->line_pos = 0;
        context->sample_pending = true;
        mutex_unlock(&smsdev.lock);
        return 0;
    }
}

static ssize_t sms_sensor_read(struct file *file, char __user *buffer,
                               size_t count, loff_t *offset)
{
    struct sms_sensor_file *context = file->private_data;
    size_t bytes_to_copy;

    if (count == 0)
        return 0;

    if (!context)
        return -EINVAL;

    mutex_lock(&context->lock);

    if (!context->sample_pending) {
        int ret;

        ret = sms_sensor_create_sample(context);
        if (ret) {
            mutex_unlock(&context->lock);
            return ret;
        }
    }

    bytes_to_copy = min(count, context->line_len - context->line_pos);
    if (copy_to_user(buffer, context->line + context->line_pos,
                     bytes_to_copy)) {
        mutex_unlock(&context->lock);
        return -EFAULT;
    }

    context->line_pos += bytes_to_copy;
    if (context->line_pos == context->line_len)
        context->sample_pending = false;

    mutex_unlock(&context->lock);
    return bytes_to_copy;
}

static long sms_sensor_ioctl(struct file *file, unsigned int command,
                             unsigned long argument)
{
    unsigned int interval_ms;

    if (command != SMS_SET_INTERVAL)
        return -ENOTTY;

    if (copy_from_user(&interval_ms, (void __user *)argument,
                       sizeof(interval_ms)))
        return -EFAULT;

    if (interval_ms == 0)
        return -EINVAL;

    mutex_lock(&smsdev.lock);
    smsdev.interval_ms = interval_ms;
    mutex_unlock(&smsdev.lock);
    return 0;
}

static const struct file_operations sms_sensor_fops = {
    .owner = THIS_MODULE,
    .open = sms_sensor_open,
    .read = sms_sensor_read,
    .release = sms_sensor_release,
    .unlocked_ioctl = sms_sensor_ioctl,
    .llseek = no_llseek,
};

static int sms_stats_show(struct seq_file *seq, void *unused)
{
    u64 read_count;
    s32 temperature;
    s32 humidity;
    s64 uptime_seconds;

    mutex_lock(&smsdev.lock);
    read_count = smsdev.read_count;
    temperature = smsdev.last_temperature_tenths;
    humidity = smsdev.last_humidity_tenths;
    uptime_seconds = div_s64(
        ktime_to_ms(ktime_sub(ktime_get(), smsdev.started_at)), 1000);
    mutex_unlock(&smsdev.lock);

    seq_printf(seq, "read_count: %llu\n", (unsigned long long)read_count);
    seq_printf(seq, "last_temperature: %d.%d\n",
               temperature / 10, temperature % 10);
    seq_printf(seq, "last_humidity: %d.%d\n",
               humidity / 10, humidity % 10);
    seq_printf(seq, "uptime_seconds: %lld\n",
               (long long)uptime_seconds);
    return 0;
}

static int sms_stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, sms_stats_show, NULL);
}

static const struct proc_ops sms_stats_proc_ops = {
    .proc_open = sms_stats_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init sms_sensor_init(void)
{
    int ret;

    mutex_init(&smsdev.lock);
    smsdev.interval_ms = SMS_DEFAULT_INTERVAL_MS;
    smsdev.started_at = ktime_get();

    ret = alloc_chrdev_region(&smsdev.devt, 0, 1, SMS_DEVICE_NAME);
    if (ret)
        return ret;

    cdev_init(&smsdev.cdev, &sms_sensor_fops);
    smsdev.cdev.owner = THIS_MODULE;
    ret = cdev_add(&smsdev.cdev, smsdev.devt, 1);
    if (ret)
        goto err_unregister_region;

    smsdev.class = class_create(THIS_MODULE, SMS_CLASS_NAME);
    if (IS_ERR(smsdev.class)) {
        ret = PTR_ERR(smsdev.class);
        smsdev.class = NULL;
        goto err_del_cdev;
    }

    smsdev.device = device_create(smsdev.class, NULL, smsdev.devt, NULL,
                                  "%s", SMS_DEVICE_NAME);
    if (IS_ERR(smsdev.device)) {
        ret = PTR_ERR(smsdev.device);
        smsdev.device = NULL;
        goto err_destroy_class;
    }

    smsdev.proc_entry = proc_create(SMS_PROC_NAME, 0444, NULL,
                                    &sms_stats_proc_ops);
    if (!smsdev.proc_entry) {
        ret = -ENOMEM;
        goto err_destroy_device;
    }

    pr_info("sms_sensor: registered major=%u minor=%u\n",
            MAJOR(smsdev.devt), MINOR(smsdev.devt));
    return 0;

err_destroy_device:
    device_destroy(smsdev.class, smsdev.devt);
    smsdev.device = NULL;
err_destroy_class:
    class_destroy(smsdev.class);
    smsdev.class = NULL;
err_del_cdev:
    cdev_del(&smsdev.cdev);
err_unregister_region:
    unregister_chrdev_region(smsdev.devt, 1);
    return ret;
}

static void __exit sms_sensor_exit(void)
{
    proc_remove(smsdev.proc_entry);
    device_destroy(smsdev.class, smsdev.devt);
    class_destroy(smsdev.class);
    cdev_del(&smsdev.cdev);
    unregister_chrdev_region(smsdev.devt, 1);

    pr_info("sms_sensor: unregistered\n");
}

module_init(sms_sensor_init);
module_exit(sms_sensor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tong Tran Dang (SE204445)");
MODULE_DESCRIPTION("SMS temperature and humidity sensor driver");
MODULE_VERSION("1.0");
