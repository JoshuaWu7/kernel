#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <vfs.h>
#include <fd.h>



/* 
 * dup2 system call
 * 
 * dup2 clones the file handle oldfd onto the file handle newfd. 
 * If newfd names an already-open file, that file is closed.
 * 
 * The two handles refer to the same "open" of the file -- that is, 
 * they are references to the same object and share the same seek pointer. 
 * Note that this is different from opening the same file twice.
 * 
 * Both filehandles must be non-negative, and, if applicable, smaller than 
 * the maximum allowed file handle number.
 * 
 * Using dup2 to clone a file handle onto itself has no effect.
 * 
 * dup2 returns newfd. On error, -1 is returned, and errno is set according to the error encountered.
 */
int sys_dup2(int oldfd, int newfd, int *new_fd_num)
{
    /* Check if given FDs are valid numbers */
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
        /* Not valid file descriptors */
        return EBADF;
    }

    if (oldfd == newfd) {
        *new_fd_num = newfd;
        return 0;
    }

    /* Get FD structure for provided FD */
    struct fd_table *cur_proc_fd_table = curproc->p_fd_table;
    struct fd *old_given_fd = cur_proc_fd_table->all_fds[oldfd];
    struct fd *new_given_fd = cur_proc_fd_table->all_fds[newfd];

    
    if (old_given_fd == NULL) {
        /* Not a valid file descriptor */
        return EBADF;
    }

    /* Old FD not null at this point */
    lock_acquire(old_given_fd->fd_lock);
    
    if (new_given_fd != NULL) {
        /* 
         * File Descriptor Exists 
         * Copy oldfd to newfd
         */
        
        /* 
         * If Vnode reference count = 1, close the vnode
         * Otherwise decrement the reference count 
         */
        struct vnode *cur_vnode = new_given_fd->fd_vnode;

        spinlock_acquire(&cur_vnode->vn_countlock);
        
        if (cur_vnode->vn_refcount == 1) {
            /* Must release lock before calling vfs_close */
            spinlock_release(&cur_vnode->vn_countlock);
            vfs_close(cur_vnode);
        }
        else {
            spinlock_release(&cur_vnode->vn_countlock);
            VOP_DECREF(cur_vnode);
        }

        /* Assign the vnode for new to the old vnode */
        fd_destroy(newfd, cur_proc_fd_table); // Destroy the existing FD

        lock_acquire(cur_proc_fd_table->fd_table_lock);
        cur_proc_fd_table->all_fds[newfd] = old_given_fd;
        lock_release(cur_proc_fd_table->fd_table_lock);

        VOP_INCREF(old_given_fd->fd_vnode);

        lock_release(old_given_fd->fd_lock);
        *new_fd_num = newfd;
        return 0;
    }
    else {
        /* 
         * File Descriptor Does not Exist
         * Create a new one with the file vnode in old 
         * Copy oldfd to newfd
         */
        lock_acquire(cur_proc_fd_table->fd_table_lock);
        cur_proc_fd_table->all_fds[newfd] = old_given_fd;
        lock_release(cur_proc_fd_table->fd_table_lock);

        VOP_INCREF(old_given_fd->fd_vnode);

        lock_release(old_given_fd->fd_lock);
        *new_fd_num = newfd;
        return 0;
    }

    /* Should not reach here */
}
