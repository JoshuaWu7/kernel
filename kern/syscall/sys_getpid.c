#include <types.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>

/* 
 * getpid system call
 *
 * getpid returns the process id of the current process.
 *
 * getpid does not fail.
 */
pid_t sys_getpid(pid_t *ret_pid)
{
    *ret_pid = curproc->p_process_id;
    return 0;
}
