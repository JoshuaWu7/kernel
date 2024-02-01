/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <spinlock.h>
#include <thread.h> /* required for struct threadarray */
#include <array.h>

struct addrspace;
struct vnode;
struct fd_table;

/*
 * Process structure.
 */
struct proc {
	char *p_name;					/* Name of this process */
	struct spinlock p_lock;			/* Lock for this structure */
	struct threadarray p_threads;	/* Threads in this process */

	/* VM */
	struct addrspace *p_addrspace;	/* virtual address space */

	/* VFS */
	struct vnode *p_cwd;			/* current working directory */

	/* FD Table */
	struct fd_table *p_fd_table;  			/* process's file descriptor table */

	/* Process ID */
	pid_t p_process_id;						/* Process's Process ID */

    /* PID table */
    struct array *p_child_process_arr;       /* Process's children PIDs */
	struct lock *p_parent_lock;
	unsigned p_child_index;  				/* Index in parent's child array */

	int p_num_children_running;

	struct proc *p_parent_process;

	struct cv *p_parent_cv;

	/* Exit Status */
	int p_exit_status;

	/* Process Exited or not */
	uint8_t p_is_zombie;
};

/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *proc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *proc_setas(struct addrspace *);

/* Get a new process ID. Returns the lowest available process ID */
pid_t get_process_id(void);

/* Deallocate a process ID. Returns 0 on success and -1 on failure. */
int dealloc_process_id(pid_t pid_to_dealloc);

/* Get PID Status. Returns 0 if available, 1 if allocated , and 2 if PID is invalid. */
uint8_t get_pid_status(pid_t pid);


#endif /* _PROC_H_ */
