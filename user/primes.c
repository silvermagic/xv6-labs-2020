//
// Created by kyle on 2020/9/14.
//

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAX_PRIMES 35

int process(int in, int out) {
    int pid, prime, number;
    pid = fork();
    if (pid == 0) {
        // Child process does not need to write to the pipe
        close(out);
        read(in, &prime, sizeof(prime));
        printf("prime %d\n", prime);
        int fds[2];
        while (read(in, &number, sizeof(int)) > 0) {
            if (number % prime != 0) {
                if (pid == 0) {
                    if (number >= MAX_PRIMES)
                        break;
                    pipe(fds);
                    pid = process(fds[0], fds[1]);
                }
                write(fds[1], &number, sizeof(int));
                if (number >= MAX_PRIMES)
                    break;
            }
        }
        // No need to write anymore
        close(fds[1]);
        if (pid > 0)
            wait(0);
    } else {
        // Parent process does not need to read from the pipe
        close(in);
    }
    return pid;
}

int main(int argc, char *argv[]) {
    int fds[2];
    pipe(fds);
    if (process(fds[0], fds[1]) > 0) {
        for (int i = 2; i <= MAX_PRIMES; i++) {
            write(fds[1], &i, sizeof(int));
        }
        // No need to write anymore
        close(fds[1]);
        wait(0);
    }
    exit(0);
}