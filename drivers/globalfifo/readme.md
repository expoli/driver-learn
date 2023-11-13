# 介绍

# globalfifo.c
## 在用户空间验证globalfifo的读写

### 创建设备文件节点“/dev/globalfifo”，具体如下：

```bash
# insmod globalfifo.ko globalfifo_major=233
# mknod /dev/globalfifo c 233 0
```

### 验证

启动两个进程，一个进程cat/dev/globalfifo&在后台执行，一个进程“echo字符串/dev/globalfifo”在前台执行：

```bash
# cat /dev/globalfifo &
[1] 234918


# echo "i want to be" > /dev/globalfifo 
i want to be
# echo "a great Chinese Linux Kernel Developer" > /dev/globalfifo 
a great Chinese Linux Kernel Developer
```

### 原理剖析

每当echo进程向/dev/globalfifo写入一串数据，cat进程就立即将该串数据显现出来


往/dev/globalfifo里面echo需要root权限，直接运行“sudo echo”是不行的，可以先执行：

```bash
sudo su
```

# globalfifo_poll.c

globalfifo_poll.c 是一个简单的 Linux 设备驱动程序，用于操作名为 "globalfifo" 的设备。这个设备驱动程序使用了 poll 机制来监控设备的读写状态。当设备可写时，程序会跳出循环并打印 "Poll monitor: can be written"。如果设备在 select 调用返回后既可读又可写，那么程序会先打印 "Poll monitor: can be read"，然后再打印 "Poll monitor: can be written"，然后跳出循环。

# 总结

阻塞与非阻塞访问是I/O操作的两种不同模式，前者在暂时不可进行I/O操作时会让进程睡眠，后者则不然。在设备驱动中阻塞I/O一般基于等待队列或者基于等待队列的其他Linux内核API来实现，等待队列可用于同步驱动中事件发生的先后顺序。使用非阻塞I/O的应用程序也可借助轮询函数来查询设备是否能立即被访问，用户空间调用select（）、poll（）或者epoll接口，设备驱动提供poll（）函数。设备驱动的poll（）本身不会阻塞，但是与poll（）、select（）和epoll相关的系统调用则会阻塞地等待至少一个文件描述符集合可访问或超时。