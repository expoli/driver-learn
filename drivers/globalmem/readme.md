# 目的
将基于虚拟的globalmem设备进行字符设备驱动的讲解。globalmem意味着“全局内存”，在globalmem字符设备驱动中会分配一片大小为GLOBALMEM_SIZE（4KB）的内存空间，并在驱动中提供针对该片内存的读写、控制和定位函数，以供用户空间的进程能通过Linux系统调用获取或设置这片内存的内容。

# 用户态测试程序

```bash
sudo su

# insmod globalmem.ko
# cat /proc/devices  | grep global
230 globalmem

# mknod /dev/globalmem c 230 0
# echo "hello world" > /dev/globalmem
# cat /dev/globalmem 
hello world
```

```bash
# tree /sys/module/globalmem/
/sys/module/globalmem/
├── coresize
├── holders
├── initsize
├── initstate
├── notes
├── parameters
│   └── globalmem_major
├── refcnt
├── sections
│   ├── __mcount_loc
│   ├── __param
│   └── __patchable_function_entries
├── srcversion
├── taint
└── uevent
```

# 总结

字符设备是3大类设备（字符设备、块设备和网络设备）中的一类，其驱动程序完成的主要工作是初始化、添加和删除cdev结构体，申请和释放设备号，以及填充file_operations结构体中的操作函数，实现file_operations结构体中的read（）、write（）和ioctl（）等函数是驱动设计的主体工作。

# 总结 2

并发和竞态广泛存在，中断屏蔽、原子操作、自旋锁和互斥体都是解决并发问题的机制。中断屏蔽很少单独被使用，原子操作只能针对整数进行，因此自旋锁和互斥体应用最为广泛。自旋锁会导致死循环，锁定期间不允许阻塞，因此要求锁定的临界区小。互斥体允许临界区阻塞，可以适用于临界区大的情况。