#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>

#define SECOND_PATH "/dev/second0"

int main(int argc, char *argv[])
{
    int fd;
    int counter = 0;
    int old_counter = 0;
    fd = open(SECOND_PATH, O_RDONLY); /* 打开设备文件 */
    if (fd != -1) {
        while (1) {
            read(fd, &counter, sizeof(unsigned int)); /* 读 */
            if (counter != old_counter) {
                printf("seconds after open %s: %d\n",SECOND_PATH, counter);
                old_counter = counter;
            }
        }
    } else {
        printf("Device open failure\n");
    }
    return 0;
}