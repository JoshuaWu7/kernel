#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <uio.h>
#include <kern/fcntl.h>
#include <fd.h>


/* 
 * read system call
 *
 * read reads up to buflen bytes from the file specified by fd, 
 * at the location in the file specified by the current seek position of the file, 
 * and stores them in the space pointed to by buf. The file must be open for reading.
 *
 * The current seek position of the file is advanced by the number of bytes read.
 * 
 * The count of bytes read is returned. This count should be positive. 
 * A return value of 0 should be construed as signifying end-of-file. 
 * On error, read returns a suitable error code for the error condition encountered.
 */
ssize_t
sys_read(int fd, void *buf, size_t buflen, ssize_t *bytes_read)
{
    int result = 0;

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
    
    /* Check flags of FD to ensure it can read */
    if ((given_fd->fd_flags & O_WRONLY) == O_WRONLY) {
        /* 
         * Doing bitwise & with a flag checks if that particular flag is set.
         * If Write Only, can't read this file. Can read in any other case.
         */
        lock_release(given_fd->fd_lock);
        return EBADF;
    }

    /* 
     * Create uio and iovec structures for reading file 
     * Usage defined in uio.h
     */
    struct uio cur_file_uio;
    struct iovec cur_file_iovec;

    /* Initialize the uio for I/O from a kernel buffer */
    uio_kinit(&cur_file_iovec, &cur_file_uio, (void *) buf, buflen, given_fd->fd_seek_pos, UIO_READ);
    cur_file_iovec.iov_ubase = (userptr_t) buf;
    cur_file_iovec.iov_len = buflen;
    cur_file_uio.uio_segflg = UIO_USERSPACE;
    cur_file_uio.uio_space = curproc->p_addrspace;

    /* Read and update FD's seek offset */
    result = VOP_READ(given_fd->fd_vnode, &cur_file_uio);
    if (result != 0) {
        lock_release(given_fd->fd_lock);
        return result;
    }

    /* Update the seek position of the FD */
    given_fd->fd_seek_pos = cur_file_uio.uio_offset;

    /* 
     * buflen is the number of bytes to read. 
     * cur_file_uio->uio_resid is the number of bytes remaining to transfer.
     * uio_resid should be 0 upon successful read of buflen bytes.
     */
    ssize_t num_bytes_read = (ssize_t) (buflen - ((&cur_file_uio)->uio_resid));
    *bytes_read = num_bytes_read;

    lock_release(given_fd->fd_lock);
    
    return 0; // Success
}
