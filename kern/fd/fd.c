#include <types.h>
#include <current.h>
#include <limits.h>
#include <proc.h>
#include <vfs.h>
#include <kern/errno.h>
#include <fd.h>


/*
 * File Descriptor Functions
 */


/* 
 * fd_create function
 * 
 * Create a new file descriptor
 * 
 * Returns 0 if success, else an error code define in <errno.h>
 * Returns the file descriptor number assigned in the fd_num argument.
 * 
 * Requires a pointer to struct fd be passed in. The struct pointed
 * to by this pointer is modified to initialize the FD.
 * Requires a pointer to the current process' FD table. 
 * 
 */
int
fd_create(char * fd_filename, int fd_flags, struct fd * file_des, int * fd_num, struct fd_table *cur_table)
{
    file_des = kmalloc(sizeof(struct fd));
    if (file_des == NULL) {
        // Malloc failed
        return ENOMEM;
    }
    
    file_des->fd_file_name = fd_filename;
    file_des->fd_lock = lock_create("fd_lock");
    file_des->fd_seek_pos = 0;
    file_des->fd_flags = fd_flags;
    
    /* Initialize FD Vnode */
    int vfs_result = vfs_open(file_des->fd_file_name, file_des->fd_flags, 0, &(file_des->fd_vnode));
    if (vfs_result != 0) {
        return vfs_result;
    }

    /* fd_num is set by fd_table_add_fd function */
    *fd_num = fd_table_add_fd(file_des, cur_table);
    if (*fd_num == -1) {
        /* Could not add FD to the FD Table */
        vfs_close(file_des->fd_vnode);
        return EMFILE;
    }
    
    // Success
    return 0;
}

/* 
 * fd_create_at_pos function
 * 
 * Create a new file descriptor for a child process
 * 
 * Returns 0 if success, else an error code define in <errno.h>
 * 
 * Requires a pointer to struct fd be passed in. The struct pointed
 * to by this pointer is modified to initialize the FD.
 * Requires a pointer to the child process' FD table. 
 * 
 */
int
fd_create_at_pos(char * fd_filename, int fd_flags, struct fd * file_des, int pos, struct fd_table *cur_table)
{
    file_des = kmalloc(sizeof(struct fd));
    if (file_des == NULL) {
        // Malloc failed
        return ENOMEM;
    }
    
    file_des->fd_file_name = fd_filename;
    file_des->fd_lock = lock_create("fd_lock");
    file_des->fd_seek_pos = 0;
    file_des->fd_flags = fd_flags;
    
    /* Initialize FD Vnode */
    int vfs_result = vfs_open(file_des->fd_file_name, file_des->fd_flags, 0, &(file_des->fd_vnode));
    if (vfs_result != 0) {
        return vfs_result;
    }

    /* Add FD to table at given position */
    lock_acquire(cur_table->fd_table_lock);
    cur_table->all_fds[pos] = file_des;
    lock_release(cur_table->fd_table_lock);
    
    // Success
    return 0;
}

/*
 * fd_destroy function
 * 
 * Destroys the provided FD.
 * Requires a pointer to the current process' FD table.
 */
void
fd_destroy(int fd, struct fd_table *cur_table)
{
    struct fd *fd_to_destroy = cur_table->all_fds[fd];

    if (fd_to_destroy == NULL) {
        return;
    }
    
    /* 
     * If reference count = 1, close the vnode
     * Otherwise decrement the reference count 
     */ 
    struct vnode *cur_vnode = fd_to_destroy->fd_vnode;
    
    spinlock_acquire(&cur_vnode->vn_countlock);

    if (cur_vnode->vn_refcount == 1) {
        /* Must release lock before calling vfs_close */
        spinlock_release(&cur_vnode->vn_countlock);

        /* If the refcount was 1, then we can close the open file */
        vfs_close(cur_vnode);
        fd_table_remove_fd(fd, cur_table);
        lock_destroy(fd_to_destroy->fd_lock);
        kfree(fd_to_destroy);
    }
    else {
        spinlock_release(&cur_vnode->vn_countlock);

        /* 
         * Don't close the open file vnode, but decrement it's refcount 
         * and deallocate this FD number.
         */
        fd_table_remove_fd(fd, cur_table);
        VOP_DECREF(cur_vnode);
    }

}



/*
 * File Descriptor Table Functions
 */


/*
 * fd_table_create function
 *
 * Creates a new file descriptor table
 * 
 * Returns a pointer to an initialized fd_table if successful.
 * If unsuccessful, returns NULL.
 * 
 * The file descriptor table is created when a new process is created.
 * Can see the proc_create_runprogram function in kern/proc/proc.c
 */
struct fd_table *
fd_table_create(void)
{
    struct fd_table *file_des_table;
    file_des_table = kmalloc(sizeof(struct fd_table));
    if (file_des_table == NULL) {
        return NULL;
    }

    file_des_table->fd_table_lock = lock_create("fd_table_lock");

    for (unsigned int i = 0; i < OPEN_MAX; i++) {
        file_des_table->all_fds[i] = NULL;
    }

    return file_des_table;
}

/*
 * fd_table_destroy function

 * Destroys the provided file descriptor table
 * 
 * Requires a pointer to a fd_table
 * If NULL, doesn't do anything.
 * If the fd_table provided is valid, then all FD's in the table
 * are deallocated and the table itself is destroyed.
 */
void
fd_table_destroy(struct fd_table * fd_table_to_destroy)
{
    if (fd_table_to_destroy == NULL) {
        return;
    }

    lock_destroy(fd_table_to_destroy->fd_table_lock);

    for (unsigned int i = 0; i < OPEN_MAX; i++) {
        fd_destroy(i, fd_table_to_destroy);
    }

    kfree(fd_table_to_destroy);
}

/*
 * fd_table_add_fd function

 * Add the provided FD to the FD Table

 * Returns -1 if error (not added to table)
 * Returns the new FD on successful add.
 */
int
fd_table_add_fd(struct fd *fd_to_add, struct fd_table *cur_fd_table)
{
    if (fd_to_add == NULL) {
        return -1;
    }

    for (unsigned int i = 0; i < OPEN_MAX; i++) {
        lock_acquire(cur_fd_table->fd_table_lock);
        if (cur_fd_table->all_fds[i] == NULL) {
            /* FD available */
            cur_fd_table->all_fds[i] = fd_to_add;
            lock_release(cur_fd_table->fd_table_lock);
            return i; // Success
        }
        lock_release(cur_fd_table->fd_table_lock);
    }

    /* Only returns -1 if the FD cannot be added to the FD Table */
    return -1;
}

/*
 * fd_table_remove_fd function
 *
 * Removes the provided FD from the FD Table
 *
 * Sets the provided fd slot in the table to NULL (deallocated).
 */
void
fd_table_remove_fd(int fd, struct fd_table *cur_fd_table)
{
    cur_fd_table->all_fds[fd] = NULL;
}

/*
 * fd_table_copy_entries function
 * 
 * Copies FD entries to new table. Should open new vnodes.
 * The child process does not share memory or open file table.
 * The state is copied into the new process, and subsequent modification in one process does not affect the other.
 */
int fd_table_copy_entries(struct fd_table *parent_table, struct fd_table *child_table)
{
    if ((parent_table == NULL) || (child_table == NULL)) {
        /* Invalid */
        return -1;
    }

    lock_acquire(parent_table->fd_table_lock);
    lock_acquire(child_table->fd_table_lock);

    /* Starting at 3 since reserved 0-2 have already been created */
    for (unsigned int i = 3; i < OPEN_MAX; i++) {
        if (parent_table->all_fds[i] != NULL) {
            /* FD exists, copy it over */
            char *new_fd_filename;
            new_fd_filename = (char *) kmalloc(PATH_MAX);

            struct fd new_child_fd;

            struct fd *curr_parent_fd = parent_table->all_fds[i];
            char *parent_fd_filename = curr_parent_fd->fd_file_name;

            strcpy(new_fd_filename, parent_fd_filename);

            fd_create_at_pos(new_fd_filename, curr_parent_fd->fd_flags, &new_child_fd, i, child_table);

            kfree(new_fd_filename);
        }
    }

    lock_release(parent_table->fd_table_lock);
    lock_release(child_table->fd_table_lock);

    return 0;
}




