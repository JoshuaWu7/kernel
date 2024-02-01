#include <types.h>
#include <syscall.h>
#include <copyinout.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <uio.h>
#include <vnode.h>
#include <fd.h>


/* 
 * Write system call
 *
 * Writes up to buflen bytes to the file specified by fd, at the location in the file
 * specified by the current seek position in the file, taking data from the space pointed to 
 * by buf. The file must be open for writing
 *
 * @param fd file descriptor number
 * @param buf pointer to user buffer space
 * @param nbytes size of the buffer data
 * @param bytes_written, the number of bytes that will actually be written in this syscall
 * @return 0 on success or an error code
 */
int sys_write(int fd, const void *buf, size_t nbytes, ssize_t *bytes_written)
{
    struct iovec cur_file_iovec;
    struct uio cur_file_uio;

    if (fd < 0 || fd >= OPEN_MAX) {
        /* Not a valid file descriptor */
        return EBADF;
    }

    /* Get FD structure for provided FD */
    struct fd_table *cur_proc_fd_table = curproc->p_fd_table;
    struct fd *given_fd = cur_proc_fd_table->all_fds[fd];

    if (given_fd == NULL) {
         /* Not a valid file descriptor, file is not open */
        return EBADF;
    }

    lock_acquire(given_fd->fd_lock);

     /* Check flags of FD to ensure it is strictly write only */
    if (!(given_fd->fd_flags & (O_RDWR | O_WRONLY))) {
        /* 
         * Doing bitwise & with a flag checks if that particular flag is set.
         * Check if the flags has either RDWR or WRONLY. (RDWR | WRONLY = 11)
         */
        lock_release(given_fd->fd_lock);
        return EBADF;
    }

    /* initialize cur_file_iovec and cur_file_uio */
    uio_kinit(&cur_file_iovec, &cur_file_uio, (void*) buf, nbytes, given_fd->fd_seek_pos, UIO_WRITE);
    cur_file_iovec.iov_ubase = (userptr_t) buf;
    cur_file_iovec.iov_len = nbytes;
    cur_file_uio.uio_segflg = UIO_USERSPACE;
    cur_file_uio.uio_space = curproc->p_addrspace;

    /* Write to the vnode given by the FD */
    int result = VOP_WRITE(given_fd->fd_vnode, &cur_file_uio);

    if (result != 0) {
        lock_release(given_fd->fd_lock);
        return result;
    }

     /* Update the seek position of the FD */
    given_fd->fd_seek_pos = cur_file_uio.uio_offset;

     /* 
      * cur_file_uio->uio_resid is the number of bytes remaining to transfer.
      * uio_resid should be 0 upon successful read of buflen bytes.
      */
    *bytes_written = (ssize_t) (nbytes - ((&cur_file_uio)->uio_resid));

    lock_release(given_fd->fd_lock);
    
    return 0; // Success
}
