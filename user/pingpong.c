//
// Created by kyle on 2020/9/14.
//

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int fds[2];
    char buf[2];
    pipe(fds);
    if (fork() == 0) {
        read(fds[0], buf, sizeof(buf));
        printf("%d: received ping\n", getpid());
        write(fds[1], "0", 1);
    } else {
        write(fds[1], "0", 1);
        read(fds[0], buf, sizeof(buf));
        printf("%d: received pong\n", getpid());
        write(fds[1], "0", 1);
        wait(0);
    }
    exit(0);
}