#include "asm-generic/errno-base.h"
#include "linux/device.h"
#include "linux/device/class.h"
#include "linux/err.h"
#include "linux/kdev_t.h"
#include "linux/kern_levels.h"
#include "linux/printk.h"
#include "linux/stddef.h"
#include "linux/timer.h"
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define SECOND_MAJOR  248

static int second_major = SECOND_MAJOR;
module_param(second_major, int, S_IRUGO);

struct second_dev {
    struct cdev cdev;   /* cdev 结构体 */
    atomic_t counter;   /* 计数器 */
    struct timer_list s_timer;  /* 定时器 */
};

static struct second_dev *second_devp;
static const struct class second_class = {
    .name = "second",
};

static void second_timer_handler(struct timer_list *timer)
{
    mod_timer(&second_devp->s_timer, jiffies + HZ); /* 触发下一次定时, 1s */
    atomic_inc(&second_devp->counter);  /* 计数器递增 */
    printk(KERN_INFO "current jiffies is %ld\n", jiffies);
}

static int second_open(struct inode *inode, struct file *filp)
{
    /* 初始化定时器，新版内核已经弃用 init_timer */
    timer_setup(&second_devp->s_timer, &second_timer_handler, 0);
    second_devp->s_timer.expires = jiffies + HZ;

    /* 添加定时器 */
    add_timer(&second_devp->s_timer);
    atomic_set(&second_devp->counter, 0); /* 初始化计数器 */
    return 0;
}

static int second_release(struct inode *inode, struct file *filp)
{
    del_timer(&second_devp->s_timer); /* 删除定时器 */
    return 0;
}

static ssize_t second_read(struct file *filp, char __user *buf, size_t count,
        loff_t *ppos)
{
    int counter;
    counter = atomic_read(&second_devp->counter);
    if (put_user(counter, (int *)buf)) /* 拷贝 counter 到 userspace */
        return -EFAULT;
    else
        return sizeof(unsigned int);
}

static const struct file_operations second_fops = {
    .owner = THIS_MODULE,
    .open = second_open,
    .release = second_release,
    .read = second_read,
};

static void second_setup_cdev(struct second_dev *dev, int index)
{
    int err, devno = MKDEV(second_major, index);

    cdev_init(&dev->cdev, &second_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
        printk(KERN_ERR "Failed to add second device\n");
}

static int __init second_init(void)
{
    int ret;
    dev_t devno = MKDEV(second_major, 0);

    if (second_major)
        ret = register_chrdev_region(devno, 1, "second");
    else {
        ret = alloc_chrdev_region(&devno, 0, 1, "second");
        second_major = MAJOR(devno);
    }
    if (ret < 0)
        return ret;

    second_devp = kzalloc(sizeof(struct second_dev), GFP_KERNEL);
    if (!second_devp) {
        ret = -ENOMEM;
        goto fail_malloc;
    }

    second_setup_cdev(second_devp, 0);

    return 0;

fail_malloc:
    unregister_chrdev_region(devno, 1);
    return ret;
}

module_init(second_init);

static void __exit second_exit(void)
{
    cdev_del(&second_devp->cdev);   /* 删除 cdev */
    kfree(second_devp);             /* 释放设备结构体内存 */
    unregister_chrdev_region(MKDEV(second_major, 0), 1); /* 注销设备号 */
}

module_exit(second_exit);

MODULE_AUTHOR("Cuyu Tang <me@expoli.tech>");
MODULE_LICENSE("GPL v2");
