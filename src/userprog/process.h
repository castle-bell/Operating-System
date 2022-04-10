#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void argument_parsing(char **argument, int* count, char* file_name);
void argument_stack(char **argument, int count, void **esp);
struct thread *find_child(tid_t pid, struct list *sibling);

#endif /* userprog/process.h */
