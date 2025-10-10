/*
 * globalfifo_dev.c - The platform device for globalfifo
 * Copyright (C) 2023 Your Name
 * Licensed under GPLv2
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fs.h>

// 这个设备号将被传递给驱动
static dev_t globalfifo_devt;

// 定义 platform_device 实例
static struct platform_device globalfifo_pdev = {
    .name = "globalfifo", // 这个名字是匹配的关键
    .id   = -1,           // 表明只有一个这样的设备
};

static int __init globalfifo_dev_init(void)
{
    int ret;

    // 1. 为我们的 "硬件" 动态分配一个字符设备号
    ret = alloc_chrdev_region(&globalfifo_devt, 0, 1, "globalfifo");
    if (ret < 0) {
        pr_err("globalfifo_dev: Failed to allocate char device region\n");
        return ret;
    }

    // 2. 将分配到的设备号作为 platform_data 存入设备结构体，以便驱动获取
    globalfifo_pdev.dev.platform_data = &globalfifo_devt;

    // 3. 向内核注册这个 platform_device
    ret = platform_device_register(&globalfifo_pdev);
    if (ret) {
        pr_err("globalfifo_dev: Failed to register platform device\n");
        // 如果注册失败，要清理已分配的设备号
        unregister_chrdev_region(globalfifo_devt, 1);
        return ret;
    }

    pr_info("globalfifo_dev: platform device registered with major %d\n", MAJOR(globalfifo_devt));
    return 0;
}

static void __exit globalfifo_dev_exit(void)
{
    // 按照与 init 相反的顺序清理
    // 1. 注销 platform_device
    platform_device_unregister(&globalfifo_pdev);
    
    // 2. 释放设备号
    unregister_chrdev_region(globalfifo_devt, 1);
    
    pr_info("globalfifo_dev: platform device unregistered\n");
}

module_init(globalfifo_dev_init);
module_exit(globalfifo_dev_exit);

MODULE_AUTHOR("Your Name <your.email@example.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Platform device for the globalfifo driver");

