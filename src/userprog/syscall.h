#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <lib/stdbool.h>

typedef int pid_t;

void syscall_init (void);
bool invalid_state(void);


#endif /* userprog/syscall.h */
