#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/errno.h>
#include <proc.h>
#include <copyinout.h>
#include <fd.h>
#include <current.h>


/* 
 * Open system call
 * 
 * Opens the file, device, or other kernel object named by the pathname filename
 *
 * @param filename pathname that specifies the file
 * @param flags specifies how to open the file 
 * @param mode provides the file permissions to use, ignored in OS/161
 * @param fd_num is the file descriptor number once the file is opened
 * @return 0 on success or an error code
 */
int
sys_open(const char *filename, int flags, mode_t mode, int *fd_num)
{
    (void) mode;
    struct fd file_des;
    int error_value;
    
    if (filename == NULL) {
        /* File name pointer was null */
        return EFAULT;
    }

    char *filename_copy = (char *) kmalloc(PATH_MAX);
    size_t actual_length;
    error_value = copyinstr((userptr_t) filename, filename_copy, PATH_MAX, &actual_length);
    if (error_value != 0) {
        kfree(filename_copy);
        return error_value;
    }

    struct fd_table *cur_fd_table = curproc->p_fd_table;

    /* Create a new fd and add it into fd_table */
    error_value = fd_create(filename_copy, flags, &file_des, fd_num, cur_fd_table);

    if (error_value != 0) {
        kfree(filename_copy);
        return error_value;
    }
    
    kfree(filename_copy);

    return 0; // Success
}
