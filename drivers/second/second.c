#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/timer.h>
#include <linux/version.h>

#define SECOND_MAJOR  248

static int second_major = SECOND_MAJOR;
module_param(second_major, int, S_IRUGO);

struct second_priv {
    struct timer_list s_timer;   /* 每个文件独立定时器 */
    atomic_t counter;            /* 每个文件独立计数器 */
};

struct second_dev {
	struct cdev cdev;
};

static struct second_dev *second_devp;
static struct class *second_class;

static void second_timer_handler(struct timer_list *timer)
{
	/* get the private_data from_timer */
	struct second_priv *priv = timer_container_of(priv, timer, s_timer);

    atomic_inc(&priv->counter);  /* 计数器递增 */
	printk(KERN_INFO "current jiffies is %ld, counter=%d\n",
           jiffies, atomic_read(&priv->counter));

    /* 重新设置下一次触发 */
    mod_timer(&priv->s_timer, jiffies + HZ); /* 触发下一次定时, 1s */
}

static int second_open(struct inode *inode, struct file *filp)
{
	struct second_priv *priv;

	/* 为每个 open 分配私有数据 */
    priv = kzalloc(sizeof(*priv), GFP_KERNEL);
    if (!priv) {
        return -ENOMEM;
	}

	atomic_set(&priv->counter, 0);

    /* 初始化定时器，新版内核已经弃用 init_timer */
    timer_setup(&priv->s_timer, &second_timer_handler, 0);
    mod_timer(&priv->s_timer, jiffies + HZ);

	/* set the priv struct to file's private_data */
	filp->private_data = priv;

    return 0;
}

static int second_release(struct inode *inode, struct file *filp)
{
	struct second_priv *priv = filp->private_data;

    if (priv) {
        timer_delete_sync(&priv->s_timer);
        kfree(priv);
        filp->private_data = NULL;
    }

    return 0;
}

static ssize_t second_read(struct file *filp, char __user *buf, size_t count,
        loff_t *ppos)
{
    struct second_priv *priv = filp->private_data;
    int counter;

    if (!priv)
        return -EINVAL;

    counter = atomic_read(&priv->counter);
    if (put_user(counter, (int __user *)buf))
        return -EFAULT;

    return sizeof(int);
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
    if (ret < 0) {
        return ret;
	}

	printk("second_major: %d\n", second_major);
    // 【修正 1】创建设备类 (class)
    // 这是你缺失的关键一步
    second_class = class_create("second_class");
    if (IS_ERR(second_class)) {
        printk(KERN_ERR "Failed to create the second class\n");
        unregister_chrdev_region(devno, 1);
        return PTR_ERR(second_class);
    }

    second_devp = kzalloc(sizeof(struct second_dev), GFP_KERNEL);
    if (!second_devp) {
        ret = -ENOMEM;
        goto fail_class;
    }

    second_setup_cdev(second_devp, 0);

    // 【修正 2】创建设备节点 (/dev/second0)
    // 应该在 init 函数中调用，并使用正确的 devno (dev_t 类型)
    if (device_create(second_class, NULL, devno, NULL, "second0") == NULL) {
        printk(KERN_ERR "Failed to create the device\n");
        ret = -EFAULT;
        goto fail_cdev;
    }
    printk(KERN_INFO "second device initialized successfully\n");

    return 0;
// 【修正 3】完善错误处理的 goto 标签
fail_cdev:
    cdev_del(&second_devp->cdev);
    kfree(second_devp);
fail_class:
    class_destroy(second_class);
    unregister_chrdev_region(devno, 1);
    return ret;
}

module_init(second_init);

static void __exit second_exit(void)
{
	dev_t devno = MKDEV(second_major, 0);
	// 顺序：销毁设备 -> 销毁类 -> 删除cdev -> 释放内存 -> 注销设备号
    device_destroy(second_class, devno); // 使用正确的 devno (dev_t 类型)
    class_destroy(second_class);
    cdev_del(&second_devp->cdev);   /* 删除 cdev */
    kfree(second_devp);             /* 释放设备结构体内存 */
    unregister_chrdev_region(devno, 1); /* 注销设备号 */
	printk(KERN_INFO "second device exited\n");
}

module_exit(second_exit);

MODULE_AUTHOR("Cuyu Tang <me@expoli.tech>");
MODULE_LICENSE("GPL v2");
