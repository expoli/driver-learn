/*
 * a simple char device driver: globalfifo
 *
 * Copyright (C) 2014 Cuyu Tang  (me@expoli.tech)
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/poll.h>

#define GLOBALFIFO_SIZE 0x1000
#define GLOBALFIFO_MAGIC 'f'
// 内核还定义了_IO（）、_IOR（）、_IOW（）和_IOWR（）这4个宏来辅助生成命令
// 由于 globalfifi 的 FIFO_CLEAR 命令不涉及数据传输，所以它可以定义为：
#define FIFO_CLEAR _IO(GLOBALFIFO_MAGIC, 0)
#define GLOBALFIFO_MAJOR 231

static int globalfifo_major = GLOBALFIFO_MAJOR;
module_param(globalfifo_major, int, S_IRUGO);

// globalfifo设备结构体
struct globalfifo_dev {
    struct cdev cdev;
    unsigned int current_len;
    unsigned char mem[GLOBALFIFO_SIZE];
    struct mutex mutex;
    wait_queue_head_t r_wait;
    wait_queue_head_t w_wait;
};

struct globalfifo_dev *globalfifo_devp;

static int globalfifo_open(struct inode *inode, struct file *filp)
{
    filp->private_data = globalfifo_devp;
    return 0;
}

static int globalfifo_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static long globalfifo_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct globalfifo_dev *dev = filp->private_data;

    switch (cmd) {
    case FIFO_CLEAR:
        mutex_lock(&dev->mutex);
        dev->current_len = 0;
        memset(dev->mem, 0, GLOBALFIFO_SIZE);
        mutex_unlock(&dev->mutex);

        printk(KERN_INFO "globalfifo is set to zero\n");
        break;

    default:
        return -EINVAL;
    }
    return 0;
}

/*
首先将设备结构体中的r_wait和w_wait等待队列头部添加到等待列表中
（意味着因调用select而阻塞的进程可以被r_wait和w_wait唤醒），
然后通过判断dev->current_len是否等于0来获得设备的可读状态，
通过判断dev->current_len是否等于GLOBALFIFO_SIZE来获得设备
的可写状态
*/
static unsigned int globalfifo_poll(struct file *filp, poll_table *wait)
{
    unsigned int mask = 0;
    struct globalfifo_dev *dev = filp->private_data;

    mutex_lock(&dev->mutex);
    poll_wait(filp, &dev->r_wait, wait);
    poll_wait(filp, &dev->w_wait, wait);

    // 读缓冲区非空
    if (dev->current_len != 0)
        // 表示设备可读
        mask |= POLLIN | POLLRDNORM;

    // 写缓冲区非满
    if (dev->current_len != GLOBALFIFO_SIZE)
        // 表示设备可写
        mask |= POLLOUT | POLLWRNORM;

    mutex_unlock(&dev->mutex);
    return mask;
}

static ssize_t globalfifo_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    int ret;
    struct globalfifo_dev *dev = filp->private_data;

    // 该宏用于定义并初始化一个名为name的等待队列元素。
    DECLARE_WAITQUEUE(wait, current);
    mutex_lock(&dev->mutex);
    // 将等待队列元素wait添加到等待队列头部q指向的双向链表中
    add_wait_queue(&dev->r_wait, &wait);

    while (dev->current_len == 0) {
        // 如果是非阻塞访问，直接返回
        // 可以通过filp->f_flags标志获得用户空间是否要求非阻塞访问
        if (filp->f_flags & O_NONBLOCK) {
            ret = -EAGAIN;
            goto out;
        }
        // 否则进程进入睡眠状态
        __set_current_state(TASK_INTERRUPTIBLE);
        // 释放互斥锁
        mutex_unlock(&dev->mutex);

        // 释放CPU，让其他进程运行
        schedule();
        // 因为是中断睡眠，所以需要判断是否有信号唤醒
        if (signal_pending(current)) {
            ret = -ERESTARTSYS;
            // 因为没有获得互斥锁，所以不能使用goto out
            goto out2;
        }
        // 重新获取互斥锁
        mutex_lock(&dev->mutex);
    }

    // 读取的字节数大于设备中的字节数
    if (count > dev->current_len)
        count = dev->current_len;

    if (copy_to_user(buf, dev->mem, count)) {
        // 向用户空间拷贝数据失败
        ret = -EFAULT;
        goto out;
    } else {
        // 向用户空间拷贝数据成功
        // 将设备中的数据前移
        memcpy(dev->mem, dev->mem + count, dev->current_len - count);
        // 更新设备中的数据长度
        dev->current_len -= count;
        printk(KERN_INFO "read %u bytes(s), current_len: %u\n", count, dev->current_len);

        // 唤醒写进程
        wake_up_interruptible(&dev->w_wait);
        ret = count;
    }

out:
    mutex_unlock(&dev->mutex);
out2:
    // 将等待队列元素wait从由q头部指向的链表中移除。
    remove_wait_queue(&dev->r_wait, &wait);
    set_current_state(TASK_RUNNING);
    return ret;
}

static ssize_t globalfifo_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
    int ret;
    struct globalfifo_dev *dev = filp->private_data;

    DECLARE_WAITQUEUE(wait, current);
    mutex_lock(&dev->mutex);
    add_wait_queue(&dev->w_wait, &wait);

    // 如果写缓冲区已满
    while (dev->current_len >= GLOBALFIFO_SIZE) {
        // 如果是非阻塞访问，直接返回
        if (filp->f_flags & O_NONBLOCK) {
            ret = -EAGAIN;
            goto out;
        }
        // 进程进入睡眠状态
        __set_current_state(TASK_INTERRUPTIBLE);
        // 主动释放互斥锁
        mutex_unlock(&dev->mutex);

        // 释放CPU，让其他进程运行
        schedule();
        // 因为是中断睡眠，所以需要判断是否有信号唤醒
        if (signal_pending(current)) {
            ret = -ERESTARTSYS;
            goto out2;
        }
        // 重新获取互斥锁
        mutex_lock(&dev->mutex);
    }

    if (count > GLOBALFIFO_SIZE - dev->current_len)
        count = GLOBALFIFO_SIZE - dev->current_len;

    if (copy_from_user(dev->mem + dev->current_len, buf, count)) {
        // 从用户空间拷贝数据失败
        ret = -EFAULT;
        goto out;
    } else {
        dev->current_len += count;
        printk(KERN_INFO "written %u bytes(s), current_len: %u\n", count, dev->current_len);

        // 唤醒读进程
        wake_up_interruptible(&dev->r_wait);
        ret = count;
    }

out:
    mutex_unlock(&dev->mutex);
out2:
    remove_wait_queue(&dev->w_wait, &wait);
    set_current_state(TASK_RUNNING);
    return ret;
}

static const struct file_operations globalfifo_fops = {
    .owner = THIS_MODULE,
    .read = globalfifo_read,
    .write = globalfifo_write,
    .open = globalfifo_open,
    .release = globalfifo_release,
    .unlocked_ioctl = globalfifo_ioctl,
    .poll = globalfifo_poll,
};

// 初始化cdev结构体，并添加到内核
static void globalfifo_setup_cdev(struct globalfifo_dev *dev, int index)
{
    int err, devno = MKDEV(globalfifo_major, index);

    cdev_init(&dev->cdev, &globalfifo_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
        printk(KERN_NOTICE "Error %d adding globalfifo%d", err, index);
}

static int __init globalfifo_init(void)
{
    int ret;
    dev_t devno = MKDEV(globalfifo_major, 0);

    if (globalfifo_major)
        ret = register_chrdev_region(devno, 1, "globalfifo");
    else {
        ret = alloc_chrdev_region(&devno, 0, 1, "globalfifo");
        globalfifo_major = MAJOR(devno);
    }
    if (ret < 0)
        return ret;

    // 为设备描述结构体分配内存
    globalfifo_devp = kzalloc(sizeof(struct globalfifo_dev), GFP_KERNEL);
    if (!globalfifo_devp) {
        ret = -ENOMEM;
        goto fail_malloc;
    }

    globalfifo_setup_cdev(globalfifo_devp, 0);

    mutex_init(&globalfifo_devp->mutex);
    // 初始化等待队列头
    init_waitqueue_head(&globalfifo_devp->r_wait);
    init_waitqueue_head(&globalfifo_devp->w_wait);

    return 0;

fail_malloc:
    unregister_chrdev_region(devno, 1);
    return ret;
}
module_init(globalfifo_init);

static void __exit globalfifo_exit(void)
{
    cdev_del(&globalfifo_devp->cdev);
    kfree(globalfifo_devp);
    unregister_chrdev_region(MKDEV(globalfifo_major, 0), 1);
}

module_exit(globalfifo_exit);

MODULE_AUTHOR("Cuyu Tang <me@expoli.tech>");
MODULE_LICENSE("GPL v2");
