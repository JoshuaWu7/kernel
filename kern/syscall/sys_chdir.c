#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/errno.h>
#include <limits.h>
#include <copyinout.h>
#include <vfs.h>

/* 
 * Chdir system call
 *
 * The current directory of the current thread is set to the directory named by pathname. 
 *
 * @param pathname the pathname that the current directory of the current thread will change to 
 */
int sys_chdir(const char *pathname)
{
    if (pathname == NULL) {
        // Not a valid pathname pointer
        return EFAULT;
    }

    char *new_pathname = kmalloc(PATH_MAX);
    size_t *pathname_length = kmalloc(sizeof(size_t));

    /* return if we cannot allocate memory */
    if (new_pathname == NULL && pathname_length == NULL) {
        return ENOMEM;
    } else if (new_pathname == NULL) {
        kfree(pathname_length);
        return ENOMEM;
    } else if (pathname_length == NULL) {
        kfree(new_pathname);
        return ENOMEM;
    }

    int err = copyinstr((const_userptr_t) pathname, new_pathname, PATH_MAX, pathname_length);

    if (err) {
        // Something wrong happened with the copyinstr
        kfree(new_pathname);
        kfree(pathname_length);
        return err;
    }

    /* Change the current directory to the new_pathname */
    int result = vfs_chdir(new_pathname);

    kfree(new_pathname);
    kfree(pathname_length);

    /* 0 if success, otherwise return error value */
    return result;
}
