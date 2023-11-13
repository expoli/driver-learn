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