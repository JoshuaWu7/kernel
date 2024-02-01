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

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <kern/fcntl.h>
#include <fd.h>
#include <limits.h>
#include <synch.h>

/* 
 * The process ID table (PID table)
 * This holds all the process IDs that are in use.
 * The index is equal to the process ID
 * The value at a given index represents whether it is allocated or not.
 * 0 means the process ID is not allocated.
 * 1 means the process ID is allocated.
 * Indexes 0 and 1 are not valid Process IDs. These will be initialized with a value of 1.
 */
uint8_t process_ID_table[PID_MAX + 1] = {0}; // Plus 1 to account for 0-index.

struct lock *pid_table_lock;

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/* 
 * Get a new process ID. Returns the lowest available process ID.
 */
pid_t 
get_process_id() 
{
    pid_t current_pid;

	lock_acquire(pid_table_lock);

	for (current_pid = PID_MIN; current_pid <= PID_MAX; current_pid++) {
        if (process_ID_table[current_pid] == 0) {
			process_ID_table[current_pid] = 1; // Now in use
			lock_release(pid_table_lock);
			return current_pid;
		}
	}
	lock_release(pid_table_lock);
	return 0;
}

/* 
 * Deallocate a process ID. Returns 0 on success and -1 on failure.
 */
int 
dealloc_process_id(pid_t pid_to_dealloc)
{
	if ((pid_to_dealloc < PID_MIN) || (pid_to_dealloc > PID_MAX)) {
		/* Invalid */
		return -1;
	}

	lock_acquire(pid_table_lock);
	process_ID_table[pid_to_dealloc] = 0; // Deallocate the PID.
	lock_release(pid_table_lock);

	/* Success */
	return 0;
} 

/* 
 * Get PID Status. Returns 0 if available, 1 if allocated, and 2 if PID is invalid.
 */
uint8_t
get_pid_status(pid_t pid)
{
	if (pid < PID_MIN || pid > PID_MAX) {
        return 2;
    }

	lock_acquire(pid_table_lock);
	uint8_t pid_status = process_ID_table[pid];
	lock_release(pid_table_lock);

	return pid_status;
}


/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}

	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	threadarray_init(&proc->p_threads);
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	/* FD Table */
	proc->p_fd_table = fd_table_create();
	if (proc->p_fd_table == NULL) {
		return NULL;
	}

    /* Child Process Array */
	proc->p_child_process_arr = array_create();
	if (proc->p_child_process_arr == NULL) {
		return NULL;
	}

	/* Lock for Child Process Structure */
	proc->p_parent_lock = lock_create("child_arr_lock");
	if (proc->p_parent_lock == NULL) {
		return NULL;
	}

	/* CV for Parent */
	proc->p_parent_cv = cv_create("parent_cv");
	if (proc->p_parent_cv == NULL) {
		return NULL;
	}

	proc->p_exit_status = 0; // Initialized to 0. Only check exit status when p_is_zombie = 1

	proc->p_is_zombie = 0;  // When it becomes a zombie, will be set to 1.

	proc->p_num_children_running = 0; // Initialize number of children to 0.

	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* FD Table */
	fd_table_destroy(proc->p_fd_table);

	/* Deallocate PID */
	dealloc_process_id(proc->p_process_id);

	/* Destroy CV*/
	cv_destroy(proc->p_parent_cv);

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	threadarray_cleanup(&proc->p_threads);
	spinlock_cleanup(&proc->p_lock);

	/* Empty array before destroying */
	int index = array_num(proc->p_child_process_arr) - 1;
	while (index > 0) {
		struct proc *cur_child = array_get(proc->p_child_process_arr, index);
		proc_destroy(cur_child);
		array_remove(proc->p_child_process_arr, index);
		index -= 1;
	}
	/* Array empty for sure */
	array_destroy(proc->p_child_process_arr);

	kfree(proc->p_name);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	/* 
	 * Initialize Process ID Table 
	 * Process IDs 0 and 1 are illegal
	 * The first valid Process ID is 2 (see limits.h)
	 */
	process_ID_table[0] = 1; // Illegal Process ID.
	process_ID_table[1] = 1; // Main Kernel Process ID.

	/* Initialize PID Table Lock */
	pid_table_lock = lock_create("pid_table_lock");
	if (pid_table_lock == NULL) {
		panic("lock_create for PID table failed\n");
	}

	/* Create the kernel process */
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
	kproc->p_process_id = 1;
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	/* 
	 * Create File Descriptors for STDOUT, STDIN, and STDERR 
	 */
	char *filename;
	filename = (char *) kmalloc(PATH_MAX);
	if (filename == NULL) {
		fd_table_destroy(newproc->p_fd_table);
		proc_destroy(newproc);
		return NULL;
	}

	int result;

	/* STDOUT */
	strcpy(filename, "con:");
	struct fd fd_stdout;
	int fd_stdout_num;
	result = fd_create(filename, O_RDONLY, &fd_stdout, &fd_stdout_num, newproc->p_fd_table);
	if (result != 0) {
		fd_table_destroy(newproc->p_fd_table);
		proc_destroy(newproc);
		return NULL;
	}

	/* STDIN */
	strcpy(filename, "con:");
	struct fd fd_stdin;
	int fd_stdin_num;
	result = fd_create(filename, O_WRONLY, &fd_stdin, &fd_stdin_num, newproc->p_fd_table);
	if (result != 0) {
		fd_table_destroy(newproc->p_fd_table); // Will also de-allocate any FD's already created
		proc_destroy(newproc);
		return NULL;
	}

	/* STDERR */
	strcpy(filename, "con:");
	struct fd fd_stderr;
	int fd_stderr_num;
	result = fd_create(filename, O_WRONLY, &fd_stderr, &fd_stderr_num, newproc->p_fd_table);
	if (result != 0) {
		fd_table_destroy(newproc->p_fd_table); // Will also de-allocate any FD's already created
		proc_destroy(newproc);
		return NULL;
	}

	kfree(filename);

	newproc->p_parent_process = curproc;

	/* Get a new process ID for the process */
	pid_t new_process_id = get_process_id(); // Define new function

	if (new_process_id == 0) {
		fd_table_destroy(newproc->p_fd_table); // Will also de-allocate any FD's already created
		proc_destroy(newproc);
		return NULL;
	}
	else {
		/* Assign child process new PID */
		newproc->p_process_id = new_process_id; 
		return newproc;
	}
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int result;
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	result = threadarray_add(&proc->p_threads, t, NULL);
	spinlock_release(&proc->p_lock);
	if (result) {
		return result;
	}
	spl = splhigh();
	t->t_proc = proc;
	splx(spl);
	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	unsigned i, num;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	/* ugh: find the thread in the array */
	num = threadarray_num(&proc->p_threads);
	for (i=0; i<num; i++) {
		if (threadarray_get(&proc->p_threads, i) == t) {
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			spl = splhigh();
			t->t_proc = NULL;
			splx(spl);
			return;
		}
	}
	/* Did not find it. */
	spinlock_release(&proc->p_lock);
	panic("Thread (%p) has escaped from its process (%p)\n", t, proc);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
