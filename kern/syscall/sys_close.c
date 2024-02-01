#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <fd.h>

/* 
 * Close system call
 *
 * The file handle fd is closed, but the same file handler may be returned again (see open, dup2, pipe, or similar calls).
 * Other files are not affected in any way, even if they are attached to the same file.
*
 * According to POSIX, even if the underlying operation fails, the file is closed anyway and the file handle becomes invalid. 
 *
 * @param fd the file that will be closed
 * @return 0 on success or an error code
 */
int sys_close(int fd)
{

    if (fd < 0 || fd >= OPEN_MAX) {
        // Not a valid file descriptor
        return EBADF;
    }
    
    struct fd_table *cur_fd_table = curproc->p_fd_table;

    lock_acquire(cur_fd_table->fd_table_lock);
    
    if (cur_fd_table->all_fds[fd] == NULL) {
        // Not a valid file descriptor, file is not open
        lock_release(cur_fd_table->fd_table_lock);
        return EBADF;
    }

    lock_release(cur_fd_table->fd_table_lock);

    /* Close the file */
    fd_destroy(fd, cur_fd_table);

    return 0;
}
