#include "linux/device.h"
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static struct cdev r_cdev;

static struct class *second_class;

static void r_cleanup (void)  
{  
    /* 先尝试删除已经存在的 class */
    if (second_class) {
        class_destroy(second_class);
    }
    int second_major = 300;
    unregister_chrdev_region(MKDEV(second_major, 0), 1); /* 注销设备号 */
        /* 创建新的 class */
    second_class = class_create("second");
    if (IS_ERR(second_class)) {
        printk(KERN_ERR "class_create() failed\n");
    }
    /* 卸载驱动程序时删除 class */
    if (second_class) {
        class_destroy(second_class);
    }
    return; 
} 

module_exit(r_cleanup);

MODULE_AUTHOR("Cuyu Tang <me@expoli.tech>");
MODULE_LICENSE("GPL v2");