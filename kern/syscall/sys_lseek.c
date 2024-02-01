#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <uio.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <fd.h>


/* 
 * lseek system call
 *
 * lseek alters the current seek position of the file handle filehandle, seeking to a new position based on pos and whence.
 * Seek positions less than zero are invalid. Seek positions beyond EOF are legal, at least on regular files.
 * 
 * If whence is
 *    SEEK_SET, the new position is pos.
 *    SEEK_CUR, the new position is the current position plus pos.
 *    SEEK_END, the new position is the position of end-of-file plus pos.
 *    anything else, lseek fails.
 * Note that pos is a signed quantity.
 */
int sys_lseek(int fd, off_t pos, int whence, int32_t *upper_32_ret, int32_t *lower_32_ret)
{
    /* Check if given FD is a valid number */
    if (fd < 0 || fd >= OPEN_MAX) {
        /* Not a valid file descriptor */
        return EBADF;
    }

    /* Get FD structure for provided FD */
    struct fd_table *cur_proc_fd_table = curproc->p_fd_table;
    struct fd *given_fd = cur_proc_fd_table->all_fds[fd];

    if (given_fd == NULL) {
        /* Not a valid file descriptor */
        return EBADF;
    }

    lock_acquire(given_fd->fd_lock);

    /* Define stat struct for SEEK_END case */
    struct stat cur_file_stat;

    /* Perform lseek depending on provide whence */
    switch(whence) {
        case SEEK_SET:
            /* 
             * New position to be set to pos 
             * In this case, pos cannot be negative.
             */
            if (pos < 0) {
                lock_release(given_fd->fd_lock);
                return EINVAL;
            }
            else {
                bool is_seekable = VOP_ISSEEKABLE(given_fd->fd_vnode);
                if (is_seekable) {
                    lock_release(given_fd->fd_lock);
                    given_fd->fd_seek_pos = pos;
                }
                else {
                    lock_release(given_fd->fd_lock);
                    return ESPIPE;
                }
            }

            break;

        case SEEK_CUR:
            /* 
             * New position is the current position plus pos 
             * Seek positions less than zero are invalid. 
             * Seek positions beyond EOF are legal, at least on regular files.
             */
            if (given_fd->fd_seek_pos + (pos) < 0) {
                lock_release(given_fd->fd_lock);
                return EINVAL;
            }
            else {
                bool is_seekable = VOP_ISSEEKABLE(given_fd->fd_vnode);
                if (is_seekable) {
                    given_fd->fd_seek_pos = given_fd->fd_seek_pos + (pos);
                }
                else {
                    lock_release(given_fd->fd_lock);
                    return ESPIPE;
                }
            }

            break;

        case SEEK_END:
            /* New position is the position of the end-of-file plus pos 
             * Seek positions less than zero are invalid. 
             * Seek positions beyond EOF are legal, at least on regular files.
             */
            VOP_STAT(given_fd->fd_vnode, &cur_file_stat);

            /* Ensure that end-of-file + pos is not less than 0 */
            if (cur_file_stat.st_size + (pos) < 0) {
                lock_release(given_fd->fd_lock);
                return EINVAL;
            }
            else {
                /* Seek to a position beyond EOF is legal */
                bool is_seekable = VOP_ISSEEKABLE(given_fd->fd_vnode);
                if (is_seekable) {
                    given_fd->fd_seek_pos = cur_file_stat.st_size + (pos);
                }
                else {
                    lock_release(given_fd->fd_lock);
                    return ESPIPE;
                }
            }

            break;

        default: 
            /* Invalid whence */
            return EINVAL;
    }
    
    *upper_32_ret = (given_fd->fd_seek_pos >> 32);
    *lower_32_ret = (given_fd->fd_seek_pos & 0xFFFFFFFF); // Gives lower 32 bits
    lock_release(given_fd->fd_lock);

    return 0; // Success
}
