#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "../threads/thread.h"
#include "../lib/user/syscall.h"

#define CLOSE_ALL -1

void syscall_init (void);

bool check_esp_validity(void *esp);
bool check_arg_validity(void *arg, int n);
bool check_buffer_validity(void *buffer, unsigned size, bool write, void* esp);
void sys_halt(void);
void sys_exit(int status);
pid_t sys_exec(const char *cmd_line);
int sys_wait(pid_t pid);
bool sys_create(const char *file, unsigned initial_size);
bool sys_remove(const char *file);
int sys_open(const char *file);
int sys_filesize(int fd);
int sys_read(int fd, void *buffer, unsigned size);
int sys_write(int fd, const void *buffer, unsigned size);
void sys_seek(int fd, unsigned position);
unsigned sys_tell(int fd);
void sys_close(int fd);
void sys_sigaction(int signum, void (*handler) (void));
void sys_sendsig(pid_t pid, int signum);
void sched_yield();
mapid_t sys_mmap (int fd, void *addr);
void sys_munmap (mapid_t mapid);
void get_argument(void *esp, int* argument, int count);

#endif /* userprog/syscall.h */
