#include <limits.h>
#include <synch.h>
#include <vnode.h>

struct lock;
struct vnode;

/* 
 * File Descriptor Structure
 *
 */
struct fd {
    char *fd_file_name;             /* file name. NAME_MAX defined in limits.h */
    struct lock *fd_lock;           /* file lock */
    struct vnode *fd_vnode;         /* vnode for file */
    off_t fd_seek_pos;              /* Seek Offset Position */
    int fd_flags;                   /* File Descriptor Flags */
};

/* 
 * File Descriptor Functions
 */
int fd_create(char *fd_filename, int fd_flags, struct fd * file_des, int * fd_num, struct fd_table *cur_table);
int fd_create_at_pos(char * fd_filename, int fd_flags, struct fd * file_des, int pos, struct fd_table *cur_table);
void fd_destroy(int fd, struct fd_table *cur_table);


/*
 * File Descriptor Table Structure
 */
struct fd_table {
    struct lock *fd_table_lock;             /* file descriptor table lock */
    
    /* 
     * array of all file descriptors
     * OPEN_MAX is defined in root/include/kern/include/limits.h 
     */
    struct fd *all_fds[OPEN_MAX];  
};

/* 
 * File Descriptor Table Functions
 */
struct fd_table *fd_table_create(void);
void fd_table_destroy(struct fd_table *fd_table_to_destroy);
int fd_table_add_fd(struct fd *fd_to_add, struct fd_table *cur_fd_table);
void fd_table_remove_fd(int fd, struct fd_table *cur_fd_table);
int fd_table_copy_entries(struct fd_table *parent_table, struct fd_table *child_table);
