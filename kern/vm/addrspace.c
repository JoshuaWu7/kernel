/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <generic_vm.h>
#include <array.h>
#include <mips/tlb.h>
#include <spl.h>
#include <spinlock.h>


/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;
	int result;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/* 
	 * Initialize the page table array
	 */
	page_table_array_init(&as->as_pt_array);
	if (&as->as_pt_array == NULL) {
		return NULL;
	}

	/* 
	 * Initialize the segment array
	 */
	segmentarray_init(&as->as_segment_array);
	if (&as->as_segment_array == NULL) {
		return NULL;
	}

	/* 
	 * Create new heap segment 
	 */
	struct segment *new_segment_0;
	new_segment_0 = kmalloc(sizeof(struct segment));
	new_segment_0->segment_start = 0;
	new_segment_0->segment_end = 0;\

	/* 
	 * Heap is read/write, but not executable 
	 */
	new_segment_0->readable = READABLE;
	new_segment_0->writeable = WRITEABLE;
	new_segment_0->executable = 0;
	new_segment_0->originally_writeable = 0;

	/* 
	 * Add heap segment to the array at index 0
	 */
	unsigned return_index;
	result = segmentarray_add(&as->as_segment_array, new_segment_0, &return_index);
	if (result != 0) {
		return NULL;
	}
	KASSERT(return_index == 0);

	/* Create spinlock for page table array */
	spinlock_init(&as->as_pt_array_spinlock);

	/* Create spinlock for segmentarray */
	spinlock_init(&as->as_segmentarray_spinlock);

	/* Define stack to be empty right now */
	as->as_stack_base = USERSTACK;
	as->as_stack_top = USERSTACK;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;
	int result;

	/* 
	 * Create new addrspace - already creates heap segment, 
	 * but it needs to be configured
	 */
	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	spinlock_acquire(&old->as_segmentarray_spinlock);
	spinlock_acquire(&old->as_pt_array_spinlock);

	/* Get number of existing segments */
	unsigned num_segments = segmentarray_num(&old->as_segment_array);
	
	/* Copy segments like in as_define_region, except heap */
	for (unsigned i = 1; i < num_segments; i++) {
		/* Get segment from old as segment array*/
		struct segment *curr_segment = segmentarray_get(&old->as_segment_array, i);
		
		/* Setup new segment */
		struct segment *new_segment;
		new_segment = kmalloc(sizeof(struct segment));
		if (new_segment == NULL) {
			spinlock_release(&old->as_segmentarray_spinlock);
			spinlock_release(&old->as_pt_array_spinlock);
			return ENOMEM;
		}
		new_segment->segment_start = curr_segment->segment_start;
		new_segment->segment_end = curr_segment->segment_end;
		new_segment->readable = curr_segment->readable;
		new_segment->writeable = curr_segment->writeable;
		new_segment->executable = curr_segment->executable;
		new_segment->originally_writeable = curr_segment->originally_writeable;

		/* Add it to the segment array */
		unsigned return_index;

		result = segmentarray_add(&newas->as_segment_array, new_segment, &return_index);
		if (result != 0) {
			spinlock_release(&old->as_segmentarray_spinlock);
			spinlock_release(&old->as_pt_array_spinlock);
			return result;
		}
	}

	/* Change heap segment start/end, at index 0 */
	struct segment *old_heap_segment = segmentarray_get(&old->as_segment_array, 0);
	struct segment *new_heap_segment = segmentarray_get(&newas->as_segment_array, 0);
	new_heap_segment->segment_start = old_heap_segment->segment_start;
	new_heap_segment->segment_end = old_heap_segment->segment_end;

	/* Change stack base/top values */
	newas->as_stack_base = old->as_stack_base;
	newas->as_stack_top = old->as_stack_top;


	spinlock_release(&old->as_segmentarray_spinlock);


	/* Copy page table */
	/* Get number of existing segments */
	unsigned num_pt_entries = page_table_array_num(&old->as_pt_array);
	
	/* Copy all page table entries, as-is */
	for (unsigned i = 0; i < num_pt_entries; i++) {
		/* Get page table entry from old as page table array*/
		struct page_table_ *curr_pte = page_table_array_get(&old->as_pt_array, i);
		
		/* Setup new page table entry */
		struct page_table_ *new_pte;
		new_pte = kmalloc(sizeof(struct page_table_));
		if (new_pte == NULL) {
			spinlock_release(&old->as_pt_array_spinlock);
			return ENOMEM;
		}

		/* Get new physical page */
		unsigned long npages = 1;
		paddr_t new_paddr = getppages(npages);
		
		if (new_paddr == 0) {
			return ENOMEM;
		}

		KASSERT(new_paddr == (new_paddr & PAGE_FRAME));

		new_pte->virtual_page_number = curr_pte->virtual_page_number;
		new_pte->physical_page_number = new_paddr;

		/* Copy memory */
		memcpy((void *) PADDR_TO_KVADDR(new_paddr), (const void *) PADDR_TO_KVADDR(curr_pte->physical_page_number), PAGE_SIZE);
		/* Add it to the page table array */
		unsigned return_index;

		result = page_table_array_add(&newas->as_pt_array, new_pte, &return_index);
		if (result != 0) {
			spinlock_release(&old->as_pt_array_spinlock);
			return result;
		}
	}


	spinlock_release(&old->as_pt_array_spinlock);

	/* Return new addrspace */
	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/* 
	 * Destroy Page Table Array
	 */
	spinlock_acquire(&as->as_pt_array_spinlock);

	/* Destroy segmentarray - Requires destroying all segments first */
	unsigned num_segments = page_table_array_num(&as->as_pt_array);
	
	/* Index to start deallocating array at (end of array) */
	int index = (int) num_segments - 1; // index must be int for signed comparison below

	/* Remove elements one by one */
	while (index >= 0) {
		/* Obtain the segment */
		struct page_table_ *cur_pte = page_table_array_get(&as->as_pt_array, index);

		/* Remove the page table entry from the array */
		page_table_array_remove(&as->as_pt_array, index);

		/* 
		 * Deallocate the physical pages associated with this segment 
		 * Requires virtual to physical address mapping 
		 */
		free_page(cur_pte->physical_page_number);

		/* Free it's memory */
		kfree(cur_pte);

		/* Decrement index */
		index -= 1;
	}

	KASSERT(page_table_array_num(&as->as_pt_array) == 0);

	/* Finally destroy array */

	spinlock_release(&as->as_pt_array_spinlock);

	/* Destroy segmentarray spinlock */
	spinlock_cleanup(&as->as_pt_array_spinlock);




	/* 
	 * Destroy segments array
	 */
	spinlock_acquire(&as->as_segmentarray_spinlock);

	/* Destroy segmentarray - Requires destroying all segments first */
	num_segments = segmentarray_num(&as->as_segment_array);
	
	/* Index to start deallocating array at (end of array) */
	index = (int) num_segments - 1; // index must be int for signed comparison below

	/* Remove elements one by one */
	while (index >= 0) {
		/* Obtain the segment */
		struct segment *cur_segment = segmentarray_get(&as->as_segment_array, index);

		/* Remove the segment from the array */
		segmentarray_remove(&as->as_segment_array, index);

		/* Free it's memory */
		kfree(cur_segment);

		/* Decrement index */
		index -= 1;
	}

	KASSERT(segmentarray_num(&as->as_segment_array) == 0);

	/* Finally destroy array */

	spinlock_release(&as->as_segmentarray_spinlock);

	/* Destroy segmentarray spinlock */
	spinlock_cleanup(&as->as_segmentarray_spinlock);
	


	/* Free the addrspace structure */
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* Don't need to do anything */
	return;
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	/* Get end of segment */
	vaddr_t seg_end = vaddr + sz;	// 1 after end of segment

	/* Create new segment */
	struct segment *new_segment;
	new_segment = kmalloc(sizeof(struct segment));
	if (new_segment == NULL) {
		return ENOMEM;
	}
	new_segment->segment_start = vaddr;
	new_segment->segment_end = seg_end;
	new_segment->readable = readable;
	new_segment->writeable = writeable;
	new_segment->executable = executable;
	new_segment->originally_writeable = NOT_WRITEABLE;

	unsigned return_index;

	spinlock_acquire(&as->as_segmentarray_spinlock);
	int result = segmentarray_add(&as->as_segment_array, new_segment, &return_index);
	if (result != 0) {
		spinlock_release(&as->as_segmentarray_spinlock);
		return result;
	}
	spinlock_release(&as->as_segmentarray_spinlock);

	/* Success */
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	/* Called after load_elf has defined all regions */

	/* Define heap region here now that all other regions are defined */

	spinlock_acquire(&as->as_segmentarray_spinlock);
	unsigned num_regions = segmentarray_num(&as->as_segment_array);
	vaddr_t largest_vaddr = 0;

	/* Start at i = 1 to skip heap segment */
	for (unsigned i = 1; i < num_regions; i++) {
		struct segment *curr_segment = segmentarray_get(&as->as_segment_array, i);
		if (curr_segment->segment_end > largest_vaddr) {
			largest_vaddr = curr_segment->segment_end;
		}

		/* If the segment is executable, set it temporarily to writeable */
		if (curr_segment->executable != 0) {
			curr_segment->originally_writeable = curr_segment->writeable;
			curr_segment->writeable = WRITEABLE;
		}
	}

	/* Since segment_end is 1 past the end of the previous segment, this is the start of the heap */

	/* Get heap segment (index 0 in array) */
	struct segment *heap_segment = segmentarray_get(&as->as_segment_array, 0);

	/* Initialize start and end of heap to be the same */
	heap_segment->segment_start = largest_vaddr;
	heap_segment->segment_end = largest_vaddr;

	spinlock_release(&as->as_segmentarray_spinlock);

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/* Set executable segments that were temporarily modified to writeable back */
	spinlock_acquire(&as->as_segmentarray_spinlock);
	unsigned num_regions = segmentarray_num(&as->as_segment_array);

	/* Start at i = 1 to skip heap segment */
	for (unsigned i = 1; i < num_regions; i++) {
		struct segment *curr_segment = segmentarray_get(&as->as_segment_array, i);

		/* If the segment is executable, set it temporarily to writeable */
		if (curr_segment->executable != 0) {
			curr_segment->writeable = curr_segment->originally_writeable;
		}
	}
	
	spinlock_release(&as->as_segmentarray_spinlock);

	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stack_top != 0);
	
	/* Initial user-level stack pointer */
	*stackptr = as->as_stack_top;

	return 0;
}

