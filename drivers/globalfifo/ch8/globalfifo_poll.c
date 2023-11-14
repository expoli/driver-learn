#include <sys/poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>

#define GLOBALFIFO_MAGIC 'f'
// 内核还定义了_IO（）、_IOR（）、_IOW（）和_IOWR（）这4个宏来辅助生成命令
// 由于 globalfifi 的 FIFO_CLEAR 命令不涉及数据传输，所以它可以定义为：
#define FIFO_CLEAR _IO(GLOBALFIFO_MAGIC, 0)
#define BUFFER_LEN 20

int main(void)
{
    int fd, num;
    fd_set rfds, wfds;
    char rd_ch[BUFFER_LEN], wr_ch[BUFFER_LEN];

    fd = open("/dev/globalfifo", O_RDONLY | O_NONBLOCK);
    if (fd != -1) {
        if (ioctl(fd, FIFO_CLEAR, 0) < 0)
            printf("ioctl command failed\n");
        while (1) {
            FD_ZERO(&rfds);
            FD_ZERO(&wfds);
            FD_SET(fd, &rfds);
            FD_SET(fd, &wfds);
            select(fd + 1, &rfds, &wfds, NULL, NULL);
            if (FD_ISSET(fd, &rfds)){
                printf("Poll monitor: can be read\n");
                break;
            }
            if (FD_ISSET(fd, &wfds))
                printf("Poll monitor: can be written\n");
        }
    } else {
        printf("Device open failure\n");
    }
    return 0;
}