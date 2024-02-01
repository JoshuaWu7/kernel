#include <types.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <fd.h>
#include <signal.h>
#include <mips/trapframe.h>
#include <kern/wait.h>

#define NTRAPCODES 13

/* 
 * _exit system call
 *
 * Cause the current process to exit. 
 * The exit code exitcode is reported back to other process(es) via the waitpid() call. 
 * 
 * The process id of the exiting process should not be reused until all processes expected to 
 * collect the exit code with waitpid have done so.
 * 
 * _exit does not return.
 */
void sys__exit(int exitcode)
{

    /* 
     * Check if parent has exited 
     * If parent has exited, then can deallocate this process
     * If parent has not exited, then should not deallocate
     */
    struct proc *parent_process = curproc->p_parent_process;

    lock_acquire(curproc->p_parent_lock);

    /* Deallocate any of my children that have completed running */
    int num_children_tracked = array_num(curproc->p_child_process_arr);
    for (int i = 0; i < num_children_tracked; i++) {
        struct proc *cur_child = array_get(curproc->p_child_process_arr, i);
        int num_elements = num_children_tracked;

        /* Check if this child has completed running */
        if (cur_child->p_is_zombie == 1) {
            /* Deallocate the child since it has finished */
            proc_destroy(cur_child);

            /* Remove from the array and shift everything else */
            array_remove(curproc->p_child_process_arr, i);

            /* Update other children indices using new array size */
            num_elements = array_num(curproc->p_child_process_arr);
            for (int j = i; j < num_elements; j++) {
                struct proc *update_child = array_get(curproc->p_child_process_arr, j);

                /* Update the p_child_index since the remove shifts the array */
                update_child->p_child_index -= 1;
            }

            /* Dont need to change any other parameters as these are set when the child process exited */
        }

        if (num_elements != num_children_tracked) {
            i -= 1; // will get incremented again.
            num_children_tracked = num_elements;
        }

        /* If the child has not completed running, continue to next process */
    }


    /* Atomically check my parent has exited */
    lock_acquire(parent_process->p_parent_lock);
    if (parent_process->p_is_zombie == 1) {
        /* Parent has exited */
        curproc->p_is_zombie = 1;

        /* Set exit status */
        curproc->p_exit_status = _MKWAIT_EXIT(exitcode);

        /* Other fields for parent don't need to be changed since it has already exited */

        lock_release(parent_process->p_parent_lock);
        lock_release(curproc->p_parent_lock);
        proc_destroy(curproc);
        thread_exit();

        /* Should not return */
    }

    /* Parent has not exited - Cannot deallocate process */
    parent_process->p_num_children_running -= 1;

    /* Set exit status */
    curproc->p_exit_status = _MKWAIT_EXIT(exitcode);

    /* 
     * Set myself to be a zombie 
     * Tells parent that I am finished running, but not deallocated, 
     * so it must reap and deallocate me when it exits.
     */
    curproc->p_is_zombie = 1;

    /* Broadcast */
    cv_broadcast(parent_process->p_parent_cv, parent_process->p_parent_lock);
   
    /* Release lock at very end to ensure atomicity */
    lock_release(parent_process->p_parent_lock);
    lock_release(curproc->p_parent_lock);

    thread_exit();

    /* Should not return */

}
