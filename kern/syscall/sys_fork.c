#include <types.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <kern/errno.h>
#include <fd.h>
#include <addrspace.h>
#include <mips/trapframe.h>

/* 
 * fork system call
 *
 * fork duplicates the currently running process. 
 * The two copies are identical, except that one (the "new" one, or "child"), 
 * has a new, unique process id, and in the other (the "parent") the process id is unchanged.
 * The process id must be greater than 0.
 * The two processes do not share memory or open file tables; 
 * This state is copied into the new process, and subsequent modification in one process does not affect the other.
 * However, the file handle objects the file tables point to are shared, so, for instance, 
 * calls to lseek in one process can affect the other.
 * 
 * On success, fork returns twice, once in the parent process and once in the child process. 
 * In the child process, 0 is returned. In the parent process, the process id of the new child process is returned.
 * 
 * On error, no new process is created.
 */
int sys_fork(struct trapframe *parent_trapframe, pid_t *ret_pid)
{
    int result;

    lock_acquire(curproc->p_parent_lock);

    /* Get name for new child process */
    char *new_child_pname;
    new_child_pname = kstrdup(curproc->p_name);
    if (new_child_pname == NULL) {
        lock_release(curproc->p_parent_lock);
        return ENOMEM;
    }

    /* 
     * Create new child process 
     * Note: the new process created by proc_create_runprogram 
     *       will have an address space and will inherit the current
     *       process's (that is, the kernel menu's) current directory.
     */
    struct proc *new_child_process = proc_create_runprogram(new_child_pname);
    if (new_child_process == NULL) {
        /* New child process cannot be created */
        kfree(new_child_pname);
        lock_release(curproc->p_parent_lock);
        return ENPROC;
    }

    pid_t new_process_id = new_child_process->p_process_id;

    /* At this point, have successfully created a new child process with unique PID */

    /* Copy the parent's file table structure */
    fd_table_copy_entries(curproc->p_fd_table, new_child_process->p_fd_table);

    /* Copy parent's address space to new child's address space */
    result = as_copy(curproc->p_addrspace, &(new_child_process->p_addrspace));
    if (result != 0) {
        proc_destroy(new_child_process); // Will also deallocate the PID
        kfree(new_child_pname);
        lock_release(curproc->p_parent_lock);
        return result;
    }

    /* Copy trapframe to the new process's trapframe */
    struct trapframe *child_trapframe;
    child_trapframe = kmalloc(sizeof(struct trapframe));
    if (child_trapframe == NULL) {
        proc_destroy(new_child_process); // Will also deallocate the PID
        kfree(child_trapframe);
        kfree(new_child_pname);
        lock_release(curproc->p_parent_lock);
        return ENOMEM;
    }

    memcpy((void *) child_trapframe, (const void *) parent_trapframe, sizeof(struct trapframe));

    /* Add the child process to the parent's child process array */
    unsigned index;
    result = array_add(curproc->p_child_process_arr, (void *) new_child_process, &index);
    if (result != 0) {
        proc_destroy(new_child_process); // Will also deallocate the PID
        kfree(new_child_pname);
        lock_release(curproc->p_parent_lock);
        return ENOMEM;
    }

    curproc->p_num_children_running += 1;

    /* Tell child what it's index is in parent's array */
    new_child_process->p_child_index = index;

    lock_release(curproc->p_parent_lock);

    /* 
     * Create new thread for child process 
     * Can pass the new trapframe created for the child to thread_fork
     */
    result = thread_fork(new_child_pname, new_child_process, fork_child_entrypoint, child_trapframe, 0);
    if  (result != 0) {
        proc_destroy(new_child_process); // Will also deallocate the PID
        kfree(child_trapframe);
        kfree(new_child_pname);
        return result;
    }

    /* Free memory and return child PID to parent */
    // kfree(child_trapframe);
    kfree(new_child_pname);

    /* Return new child PID to parent process */
    *ret_pid = new_process_id;

    return 0;
}

/* 
 * fork_child_entrypoint function
 * 
 * Arguments for this function are defined by the arguments that are required by thread_fork
 * (see usage in sys_fork above).
 * 
 * This function is responsible for setting the new thread's address space.
 * 
 * Then it calls the enter_forked_process function in syscall.c to set the appropriate
 * return values in the trapframe.
 */
void fork_child_entrypoint(void *data1, unsigned long data2)
{
    /* Allocate the trapframe on stack */
    struct trapframe child_trapframe;
    
    // struct trapframe *heap_trapframe = (struct trapframe *) data1;

    /* Must copy the trapframe to the new thread's stack */
    memcpy((void *) (&child_trapframe), (const void *) data1, sizeof(struct trapframe));

    /* Don't require data2 */
    (void) data2;

    /* Activate */
    as_activate();

    /* Deallocate heap copy of trapframe */
    kfree(data1);

    /* Call enter_forked_process in syscall.c */
    enter_forked_process(&child_trapframe);
}
