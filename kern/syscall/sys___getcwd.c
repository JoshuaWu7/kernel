#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <uio.h>
#include <vfs.h>
#include <addrspace.h>
#include <fd.h>


/* 
 * __getcwd system call
 * 
 * The name of the current directory is computed and stored in buf, an area of size buflen. 
 * The length of data actually stored, which must be non-negative, is returned.
 * Note: this call behaves like read - the name stored in buf is not 0-terminated.
 * 
 * On success, __getcwd returns the length of the data returned. 
 * On error, -1 is returned, and errno is set according to the error encountered.
 */
int sys___getcwd(char *buf, size_t buflen, ssize_t *sys___getcwd_bytes_returned)
{
    int result = 0;

    /* 
     * Create uio and iovec structures for reading file 
     * Usage defined in uio.h
     */
    struct uio cwd_uio;
    struct iovec cwd_iovec;

    /* Create a kernel buffer - Will copyout from this to userspace later */
    void *kern_buf = kmalloc(buflen);
    if (kern_buf == NULL) {
        return ENOMEM; // Out of memory
    }

    cwd_iovec.iov_kbase = kern_buf;
    cwd_iovec.iov_len = buflen;

    /* Initialize the uio for I/O from a kernel buffer */
    uio_kinit(&cwd_iovec, &cwd_uio, kern_buf, buflen, 0, UIO_READ);

    /* Assign uio for user data and part of current process's address space */

    /* Get the cwd */
    result = vfs_getcwd(&cwd_uio);
    if (result != 0) {
        kfree(kern_buf);
        return result;
    }

    /* Copy out to user space */
    result = copyout(kern_buf, (userptr_t) buf, buflen);
    if (result != 0) {
        kfree(kern_buf);
        return EFAULT;
    }

    /* 
     * buflen is the number of bytes to read. 
     * cur_file_uio->uio_resid is the number of bytes remaining to transfer.
     * uio_resid should be 0 upon successful read of buflen bytes.
     */
    ssize_t num_bytes = (ssize_t) (buflen - ((&cwd_uio)->uio_resid));
    *sys___getcwd_bytes_returned = num_bytes;

    kfree(kern_buf);

    return 0;
}
