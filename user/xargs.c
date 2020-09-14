//
// Created by kyle on 2020/9/14.
//

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

void do_command(int argc, char *argv[]) {
    //
    int find = 0, pid = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "|") == 0) {
            find = i + 1;
            break;
        }
    }
    int fds[2];
    if (find > 0) {
        pipe(fds);
        pid = fork();
        if (pid == 0) {
            close(0);
            dup(fds[0]);
            close(fds[0]);
            close(fds[1]);
            do_command(argc - find, argv + find);
            exit(0);
        } else {
            close(1);
            dup(fds[1]);
            close(fds[0]);
            close(fds[1]);
        }
    } else {
        find = argc;
    }

    if (strcmp(argv[0], "xargs") == 0) {
        char *params[MAXARG] = {0};
        memcpy(params, argv + 1, sizeof(char *) * (find - 1));
        char buf[256];
        params[find - 1] = buf;
        int i = 0;
        while (read(0, &buf[i], sizeof(char)) > 0) {
            if (buf[i++] == '\n') {
                buf[i - 1] = '\0';
                if (fork() == 0) {
                    exec(params[0], params);
                }
                wait(0);
                i = 0;
            }
        }
    }
    if (pid > 0)
        wait(0);
}

int main(int argc, char *argv[]) {
    if (argc < 1) {
        exit(0);
    }
    do_command(argc, argv);
    exit(0);
}