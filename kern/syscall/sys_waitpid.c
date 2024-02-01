#include <types.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <limits.h>
#include <kern/errno.h>
#include <copyinout.h>
#include <fd.h>

/* 
 * waitpid system call
 *
 * Wait for the process specified by pid to exit, and return an encoded exit status in the integer pointed to by status. 
 * If that process has exited already, waitpid returns immediately. 
 * If that process does not exist, waitpid fails.
 * 
 * A process moves from "has exited already" to "does not exist" 
 * when every process that is expected to collect its exit status with waitpid has done so.
 * 
 * The options argument should be 0. You are not required to implement any options. 
 * (However, your system should check to make sure that requests for options you do not support are rejected.)
 * 
 * waitpid returns the process id whose exit status is reported in status. In OS/161, this is always the value of pid.
 * If you implement WNOHANG, and WNOHANG is given, and the process specified by pid has not yet exited, waitpid returns 0.
 * On error, a suitable errno is returned.
 */
int sys_waitpid(pid_t pid, int *status, int options, pid_t *ret_pid)
{
    int result;
    int exit_status;

    if (options != 0) {
        return EINVAL; // Invalid or unsupported options.
    }

    lock_acquire(curproc->p_parent_lock);

    /* Check if PID is outside of limits */
    if (pid < PID_MIN || pid > PID_MAX) {
        lock_release(curproc->p_parent_lock);
        return ESRCH;
    }

    /* Check if the PID is not allocated */
    uint8_t pid_status = get_pid_status(pid);
    if (pid_status == 0) {
        lock_release(curproc->p_parent_lock);
        return ESRCH;
    }

    /* Check if the PID is a child of the current process */

    /* Get the number of children tracked by me */
    unsigned num_children = array_num(curproc->p_child_process_arr);

    int found_child = 0;

    for (unsigned i = 0; i < num_children; i++) {
        struct proc *cur_child = array_get(curproc->p_child_process_arr, i);
        if (cur_child->p_process_id == pid) {
            /* Found the provided child */
            found_child = 1;
            if (cur_child->p_is_zombie == 1) {
                /* Child has already exited */
                exit_status = cur_child->p_exit_status;
                break;
            }
            else {
                while(cur_child->p_is_zombie == 0) {
                    /* 
                     * The child has not exited
                     * Sleep until the child exits 
                     */
                    cv_wait(curproc->p_parent_cv, curproc->p_parent_lock);

                    /* 
                     * After waking up, if the pid specified has exited,
                     * we exit out of the while loop.
                     */
                }

                /* The PID specified has exited */
                exit_status = cur_child->p_exit_status;
                break;
            }
        }

        /* Otherwise, check the next child */
    }

    /* The provided PID is not a child of the current process */
    if (found_child == 0) {
        lock_release(curproc->p_parent_lock);
        return ECHILD;
    }


    /* Copyout the exit status to userspace */
    if (status == NULL) {
        /* Don't update status, simply return pid */
        *ret_pid = pid;
        lock_release(curproc->p_parent_lock);
        return 0;
    }

    /* status pointer is provided, so do copyout */
    result = copyout(&exit_status, (userptr_t) status, sizeof(int));
    if (result != 0) {
        lock_release(curproc->p_parent_lock);
        return result;
    }

    /* copyout complete, success */
    *ret_pid = pid;
    lock_release(curproc->p_parent_lock);
    return 0;
}