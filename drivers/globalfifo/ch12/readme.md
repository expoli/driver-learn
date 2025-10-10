# 12　总结

## 设计思路
### globalfifo_dev (设备模块)

- 职责: 模拟一个硬件设备的存在。
- 核心工作:
  1. 在 init 函数中，向内核注册一个 platform_device。这个设备的名字是 "globalfifo"。
  2. 为了让驱动能找到它，该设备需要提供必要的资源。在这里，最重要的资源就是字符设备的主/次设备号 (dev_t)。
  3. 我们会先用 alloc_chrdev_region 动态分配一个设备号，然后将这个 dev_t 类型的设备号通过 platform_data 传递给驱动。
  4. 在 exit 函数中，反向操作：注销 platform_device 并释放设备号。

### globalfifo_drv (驱动模块)

- 职责: 提供操作硬件的逻辑（在这里是操作内存FIFO）。
- 核心工作:
  1. 注册一个 platform_driver，其名字也必须是 "globalfifo"，这样内核才能将它和设备匹配起来。
  2. 当匹配成功时，内核调用驱动的 probe 函数。
  3. 在 probe 函数中:
       - 从匹配到的 platform_device 的 platform_data 中获取设备号。
       - 使用 devm_kzalloc 分配设备私有结构体 globalfifo_dev。
       - 初始化 cdev 并使用获取到的设备号将其添加到系统。
       - 创建 class 和 device，以便在 /dev 目录下自动生成设备文件 /dev/globalfifo。
       - 初始化互斥锁和等待队列。
  4. 在 remove 函数中，执行与 probe 相反的清理操作（大部分清理工作由内核的设备资源管理 devm_* 系列函数和platform核心自动完成）。

## 问题解决

> 问题根源：Device 'globalfifo' does not have a release() function

这是一个非常棒的错误报告，它触及了Linux设备模型中一个核心且重要的概念：对象生命周期管理。这个错误日志非常具有指导性，我们来一步步解析它。

这是内核给出的最直接、最明确的错误信息。它告诉你，你注册的名为 'globalfifo' 的设备，缺少一个关键的 release() 函数。

为什么需要 release() 函数？

对象生命周期: 在Linux内核中，struct device（以及它内嵌的 struct kobject）的生命周期是由引用计数管理的。

当你调用 platform_device_register() 时，内核会增加该设备的引用计数。
当驱动程序与设备绑定时，引用计数也可能增加。
当你调用 platform_device_unregister()（就像你在 globalfifo_dev_exit 中所做的），或者驱动程序与设备解绑时，内核会调用 put_device() 来减少引用计数。
最后的告别: 当引用计数减到 零 时，意味着这个设备对象不再被内核的任何部分所需要。此时，内核必须销毁这个对象，释放它占用的内存。

release() 的职责: 内核本身并不知道如何释放你创建的 struct device 结构体，因为这个结构体可能是你用 kmalloc 动态分配的，也可能是一个静态全局变量。因此，内核把这个“最后释放”的职责交还给了创建者。dev->release 就是一个函数指针，指向你提供的释放函数。当引用计数为零时，内核会回调这个函数。

错误触发流程 (根据你的日志):

你执行 rmmod globalfifo_dev。
内核调用 globalfifo_dev_exit()。
globalfifo_dev_exit() 调用 platform_device_unregister()。
platform_device_unregister() 调用 put_device()，减少了设备的引用计数。
此时，引用计数变为0。
内核准备释放 struct device 对象，它去查找 dev->release 函数指针。
它发现这个指针是 NULL！内核不知道该如何清理，于是它打印了这条著名的警告信息，因为它不知道这是否会导致内存泄漏。
如何修正
你需要在 globalfifo_dev.c 中为你的 platform_device 提供一个 release 函数。

关键点：在我们的例子中，globalfifo_pdev 是一个静态全局变量，它的内存不是动态分配的，而是存在于模块的 .data 段。因此，当模块被卸载时，这块内存会自动被回收。

所以，我们的 release 函数实际上什么都不用做！但我们必须提供一个空的函数来“安抚”内核，告诉它：“我知道我在做什么，这个设备不需要特殊的内存释放操作”。