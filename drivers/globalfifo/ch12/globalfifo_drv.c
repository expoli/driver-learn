/*
 * a simple char device driver: globalfifo
 *
 * Copyright (C) 2014 Cuyu Tang  (me@expoli.tech)
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#define GLOBALFIFO_SIZE 0x1000
#define GLOBALFIFO_MAGIC 'f'
// 内核还定义了_IO（）、_IOR（）、_IOW（）和_IOWR（）这4个宏来辅助生成命令
// 由于 globalfifi 的 FIFO_CLEAR 命令不涉及数据传输，所以它可以定义为：
#define FIFO_CLEAR _IO(GLOBALFIFO_MAGIC, 0)

// 全局的 class 指针
static struct class *globalfifo_class;

// globalfifo设备结构体
struct globalfifo_dev {
    struct cdev cdev;
    unsigned int current_len;
    unsigned char mem[GLOBALFIFO_SIZE];
    struct mutex mutex;
    wait_queue_head_t r_wait;
    wait_queue_head_t w_wait;
    // 异步结构体指针
    struct fasync_struct *async_queue;
};


// 支持异步通知的globalfifo设备驱动fasync（）函数
static int globalfifo_fasync(int fd, struct file *filp, int mode)
{
    struct globalfifo_dev *dev = filp->private_data;
    return fasync_helper(fd, filp, mode, &dev->async_queue);
}

static int globalfifo_open(struct inode *inode, struct file *filp)
{
    // 通过 inode 中的 cdev 找到容器结构体的指针
    struct globalfifo_dev *dev = container_of(inode->i_cdev, struct globalfifo_dev, cdev);
    filp->private_data = dev;
    return 0;
}

// 增加异步通知后的globalfifo设备驱动release（）函数
static int globalfifo_release(struct inode *inode, struct file *filp)
{
    // 释放异步通知
    globalfifo_fasync(-1, filp, 0);
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

        pr_info("globalfifo is set to zero\n");
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
        pr_info("read %lu bytes(s), current_len: %u\n", count, dev->current_len);

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
/*
在globalfifo设备被正确写入之后，它变得可读，
这个时候驱动应释放SIGIO信号，以便应用程序捕获

支持异步通知的globalfifo设备驱动写函数
*/
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
        pr_info("written %lu bytes(s), current_len: %u\n", count, dev->current_len);

        // 唤醒读进程
        wake_up_interruptible(&dev->r_wait);

        if (dev->async_queue) {
            // 向应用程序发送SIGIO信号
            kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
            pr_debug("%s kill SIGIO\n", __func__);
        }
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
	.unlocked_ioctl = globalfifo_ioctl,
	.poll = globalfifo_poll,
	.fasync = globalfifo_fasync,
	.open = globalfifo_open,
	.release = globalfifo_release,
};

// probe 函数：当内核匹配到同名 device 和 driver 时调用
static int globalfifo_probe(struct platform_device *pdev)
{
    int ret;
    dev_t devt;
    struct globalfifo_dev *dev;
    dev_t *p_devt = pdev->dev.platform_data; // 从 platform_device 获取数据
    if (!p_devt) {
        pr_err("globalfifo_drv: No device number found in platform_data\n");
        return -EINVAL;
    }
    devt = *p_devt;
    // 使用 devm_kzalloc，内核会在驱动卸载时自动释放内存
    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        return -ENOMEM;
    }
    // 将设备实例指针存入 platform_device，以便 remove 函数可以访问
    platform_set_drvdata(pdev, dev);
    // 初始化 cdev 并添加到系统
    cdev_init(&dev->cdev, &globalfifo_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, devt, 1);
    if (ret) {
        pr_err("globalfifo_drv: Failed to add cdev\n");
        return ret;
    }

    // 初始化互斥锁和等待队列
    mutex_init(&dev->mutex);
    init_waitqueue_head(&dev->r_wait);
    init_waitqueue_head(&dev->w_wait);

    // 创建设备节点 /dev/globalfifo
    device_create(globalfifo_class, NULL, devt, NULL, "globalfifo");
    pr_info("globalfifo_drv: probed and initialized successfully\n");
    return 0;
}
// remove 函数：当设备或驱动被移除时调用
static void globalfifo_remove(struct platform_device *pdev)
{
    struct globalfifo_dev *dev = platform_get_drvdata(pdev);

    // 按照与 probe 相反的顺序清理
    device_destroy(globalfifo_class, dev->cdev.dev);
    cdev_del(&dev->cdev);
    // devm_kzalloc 分配的内存无需手动释放
    pr_info("globalfifo_drv: removed\n");
}
// 定义 platform_driver 结构体
static struct platform_driver globalfifo_platform_driver = {
    .driver = {
        .name = "globalfifo", // 这个名字是匹配的关键
    },
    .probe = globalfifo_probe,
    .remove = globalfifo_remove,
};


static int __init globalfifo_drv_init(void)
{
    int ret;

    // 1. 创建 class
    globalfifo_class = class_create("globalfifo_class");
    if (IS_ERR(globalfifo_class)) {
        pr_err("globalfifo_drv: Failed to create class\n");
        return PTR_ERR(globalfifo_class);
    }
        
    // 2. 注册 platform driver
    ret = platform_driver_register(&globalfifo_platform_driver);
    if (ret) {
        pr_err("globalfifo_drv: Failed to register platform driver\n");
        class_destroy(globalfifo_class);
    }
    return ret;
}

static void __exit globalfifo_drv_exit(void)
{
    platform_driver_unregister(&globalfifo_platform_driver);
    class_destroy(globalfifo_class);
}

module_init(globalfifo_drv_init);
module_exit(globalfifo_drv_exit);

MODULE_AUTHOR("Cuyu Tang <me@expoli.tech>");
MODULE_LICENSE("GPL v2");
