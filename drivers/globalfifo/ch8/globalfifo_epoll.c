#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <strings.h>

#define GLOBALFIFO_MAGIC 'f'
// 内核还定义了_IO（）、_IOR（）、_IOW（）和_IOWR（）这4个宏来辅助生成命令
// 由于 globalfifi 的 FIFO_CLEAR 命令不涉及数据传输，所以它可以定义为：
#define FIFO_CLEAR _IO(GLOBALFIFO_MAGIC, 0)
#define BUFFER_LEN 20
void main(void)
{
	int fd;

	fd = open("/dev/globalfifo", O_RDONLY | O_NONBLOCK);
	if (fd != -1) {
		struct epoll_event ev_globalfifo;
		int err;
		int epfd;

		if (ioctl(fd, FIFO_CLEAR, 0) < 0)
			printf("ioctl command failed\n");

		epfd = epoll_create(1);
		if (epfd < 0) {
			perror("epoll_create()");
			return;
		}

		bzero(&ev_globalfifo, sizeof(struct epoll_event));
		ev_globalfifo.events = EPOLLIN | EPOLLPRI;

		err = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev_globalfifo);
		if (err < 0) {
			perror("epoll_ctl()");
			return;
		}
		err = epoll_wait(epfd, &ev_globalfifo, 1, 15000);
		if (err < 0) {
			perror("epoll_wait()");
		} else if (err == 0) {
			printf("No data input in FIFO within 15 seconds.\n");
		} else {
			printf("FIFO is not empty\n");
		}
		err = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev_globalfifo);
		if (err < 0)
			perror("epoll_ctl()");
	} else {
		printf("Device open failure\n");
	}
}
