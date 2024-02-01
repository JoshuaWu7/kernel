#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/errno.h>
#include <proc.h>
#include <current.h>
#include <array.h>
#include <addrspace.h>


/* 
 * sbrk system call
 * 
 * The "break" is the end address of a process's heap region. 
 * The sbrk call adjusts the "break" by the amount amount. 
 * It returns the old "break". Thus, to determine the current "break", call sbrk(0).
 * 
 * The heap region is initially empty, so at process startup, the beginning of the heap region 
 * is the same as the end and may thus be retrieved using sbrk(0).
 * 
 * In OS/161, the initial "break" must be page-aligned, and sbrk only need support values 
 * of amount that result in page-aligned "break" addresses. Other values of amount may be rejected.
 * 
 * While one can lower the "break" by passing negative values of amount, one may not set the 
 * end of the heap to an address lower than the beginning of the heap. Attempts to do so must be rejected.
 *
 * @param amount: the amount to adjust "break" by.
 * @return the previous value of the "break" on success or an error code with ((void *)-1) as the return address.
 */
int
sys_sbrk(ssize_t amount, int32_t *ret_addr)
{
    /* Get current address space */
    struct addrspace *curr_as = curproc->p_addrspace;

    /* Acquire segmentarray spinlock */
    spinlock_acquire(&curr_as->as_segmentarray_spinlock);

    /* Get heap segment for this addrspace --> defined at index 0 */
    struct segment *heap_segment = segmentarray_get(&curr_as->as_segment_array, 0);

    KASSERT(heap_segment != NULL);

    vaddr_t old_break = heap_segment->segment_end;
    
    /* Check for 0 amount */
    if (amount == 0) {
        /* Return the current "break" */
        *ret_addr = old_break;
        spinlock_release(&curr_as->as_segmentarray_spinlock);
        return 0;
    }

    /* Check if the amount is page aligned */
    if ((amount % PAGE_SIZE) != 0) {
        /* Invalid amount requested, return EINVAL */
        spinlock_release(&curr_as->as_segmentarray_spinlock);
        *ret_addr = -1;
        return EINVAL;
    }

    /* Check if the new allocated space grows into stack */
    if ((heap_segment->segment_end + amount) >= curr_as->as_stack_top) {
        /* Adding amount grows into the stack region */
        spinlock_release(&curr_as->as_segmentarray_spinlock);
        *ret_addr = -1;
        return EINVAL;
    }

    /* 
     * Since amount can be negative, check if the new break is below heap_start 
     * It's okay for segment_end + amount to equal segment_start, but it cannot
     * go below it. 
     */
    if ((heap_segment->segment_end + amount) < heap_segment->segment_start ) {
        /* new break would go below heap_start */
        spinlock_release(&curr_as->as_segmentarray_spinlock);
        *ret_addr = -1;
        return EINVAL;
    }

    /* Assign new heap segment end */
    heap_segment->segment_end += amount;
    *ret_addr = old_break;

    /* 
     * Don't need to free or allocate pages here as 
     * a page that doesn't exist would be created in vm_fault.
     * If the amount is < 0, the heap bound is changed, and we could
     * deallocate the pages here, but it's not strictly necessary as
     * the pages would be deallocated when as_destroy is called.
     * (There is no observable behaviour change)
     */
    
    /* Release spinlock */
    spinlock_release(&curr_as->as_segmentarray_spinlock);

    /* Success */
    return 0;
}
