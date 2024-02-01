#define SEGMENTINLINE
#define PAGETABLEINLINE


#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <generic_vm.h>


/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;


/* Initialize Core Map */
struct core_map *generic_vm_core_map;
struct spinlock core_map_spinlock;
unsigned long num_core_map_entries;               /* Number of Core Map Entries */


/* Physical Space Variables */
paddr_t physical_start;
int physical_page_start;
paddr_t physical_end;
int physical_page_end;

int vm_bootstrap_complete = 0;

/*
 * Helper function to initilaize the core map
 */
int
core_map_init()
{
	/* 
	 * Get firstpaddr from ram.c 
	 * This becomes the address of the core map.
	 * The first actual address of physical space that we track is 
	 * this address + the size of the core map
	 */
	paddr_t start_mem = ram_stealmem(0);
	paddr_t end_mem = ram_getsize();	

	physical_end = end_mem;

	unsigned long total_pages = (end_mem - start_mem) / PAGE_SIZE;
	unsigned long core_map_pages = ((total_pages * sizeof(struct core_map_entry)) + PAGE_SIZE - 1) / PAGE_SIZE;

	paddr_t core_map_paddr = ram_stealmem(core_map_pages);

	/* ram_stealmem returns 0 if it can't allocate physical memory */
	if (core_map_paddr == 0) {
		return ENOMEM;
	}

	/* 
	 * Get the virtual address of the core map 
	 */
	core_map = (struct core_map_entry *) PADDR_TO_KVADDR(core_map_paddr);

	/* Check that virtual address translation done properly */
	if (core_map == NULL) {
		return ENOMEM;
	}

	/* The actual start of physical address space should not include the core map page */
	physical_start = core_map_paddr + PAGE_SIZE;


	/* Determine number of pages we need to track */
	unsigned long num_pages_to_track = (physical_end - physical_start) / PAGE_SIZE;
	num_core_map_entries = num_pages_to_track;

	/* Check bounds of pages */
	KASSERT(physical_start < physical_end);

	/* Initialize entries core map to free (all those not stolen with ram_stealmem) */
	for (unsigned long i = 0; i < num_pages_to_track; i++) {
		core_map[i].page_allocated = PAGE_FREE;
		core_map[i].num_pages_track = 0;
	}

	/* Initialize the core map spinlock */
	spinlock_init(&core_map_spinlock);

	/* Success */
	return 0;
}




/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
curproc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Intializes the GENERIC VM core map
 */
void
vm_bootstrap(void)
{
	int result;
	
	/* Initialize Core Map */
	result = core_map_init();
	if (result != 0) {
		panic("vm bootstrap core map init failed\n");
	}

	/* 
	 * Don't need to initialize page tables here 
	 * as this is a per-addrspace thing
	 * This is done when as_create is called.
	 */

	vm_bootstrap_complete = 1;

}


/* 
 * Gets a new physical page address and allocate npages (consecutively) to the core map
 */
paddr_t
getppages(unsigned long npages)
{	
	paddr_t addr;

	if (vm_bootstrap_complete == NOT_COMPLETE) {
		/* If vm bootstrap not complete, do what DUMBVM does and steal mem */
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
	}
	else {
		spinlock_acquire(&core_map_spinlock);
		unsigned long cm_entries = num_core_map_entries;

		addr = 0; /* In case cannot find contiguous free pages */

		bool contiguous_found = true; /* Default found */

		for (unsigned long int i = 0; i < cm_entries; i++) {
			if (core_map[i].page_allocated == PAGE_FREE) {
				addr = physical_start + (i * PAGE_SIZE);

				for (unsigned long int pages_found = 1; pages_found < npages; pages_found++) {
					if (core_map[pages_found].page_allocated == PAGE_ALLOCATED) {
						contiguous_found = false; /* No contiguous found */
						break;
					}
				}

				if (contiguous_found) {
					/* Then set pages as allocated and break out*/
					for (unsigned long int j = i; j < (i + npages); j++) {
						core_map[j].num_pages_track = npages;
						core_map[j].page_allocated = PAGE_ALLOCATED; /* Mark page as allocated */
						npages -= 1;
					}
					break;
				}

				/* Otherwise check next pages */
			}
		}

		spinlock_release(&core_map_spinlock);
	}

	return addr;
}



/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	/* Return 0 if npages = 0 as kmalloc will return NULL */
	if (npages == 0) {
		return 0; 
	}
	
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

/*
 * Frees an allocation of consecutive pages given a virtual kernel page. 
 * Consective pages will be freed from the core map.
 */
void
free_kpages(vaddr_t addr)
{
	/* First get the paddr of the page */
	paddr_t paddr = KVADDR_TO_PADDR(addr);
	
	/* Align it to the page */
	paddr_t ppage_number = PADDR_TO_PPAGE(paddr);

	/* Find it's index */
	unsigned long actual_paddr = (ppage_number - physical_start);
	unsigned long index =  actual_paddr / PAGE_SIZE;

	/* Index must be less than the number of core map entries */
	/* Free the page */
	spinlock_acquire(&core_map_spinlock);

	/* 
	 * Get number of pages tracked by the first page in the allocation 
	 * This is really only applicable if npages was > 1
	 */
	unsigned long num_pages_tracked = core_map[index].num_pages_track;

	if (num_pages_tracked == 1) {
		/* Free the single page */
		core_map[index].page_allocated = PAGE_FREE;
	}
	else {
		/* Free all pages in the original allocation */
		for (unsigned long i = index; i < index + num_pages_tracked - 1; i++) {
			core_map[i].page_allocated = PAGE_FREE;
			core_map[i].num_pages_track = 0;
		}
	}

	/* Release spinlocks */
	spinlock_release(&core_map_spinlock);
}

/*
 * Frees a single page and updates the core map
 */
void
free_page(paddr_t addr)
{
	/* Invalid addr */
	if (addr == 0) {
		return;
	}

	/* addr not page aligned */
	if ((addr % PAGE_SIZE) != 0) {
		return;
	}

	spinlock_acquire(&core_map_spinlock);

	/* Calculate the index to the core map */
	unsigned long actual_paddr = (addr - physical_start);
	unsigned long index =  actual_paddr / PAGE_SIZE;

	/* Free the page */
	core_map[index].page_allocated = PAGE_FREE;

	spinlock_release(&core_map_spinlock);
}

void
vm_tlbshootdown_all(void)
{
	panic("GENERIC VM tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("GENERIC VM tried to do tlb shootdown?!\n");
}

/*
 * VM Fault is called by the MIPS exection handler upon TLB misses
 */
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	struct addrspace *as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	int err = vm_fault_helper(faulttype, faultaddress, as);
	if (err != 0) {
		return err;
	}

	// Success
	return 0;
}

/*
 * Given the faultaddress and address space, handles the VM fault according to the faulttype
 */
int
vm_fault_helper(int faulttype, vaddr_t faultaddress, struct addrspace *as) 
{	
	vaddr_t actual_address = faultaddress;
	faultaddress &= PAGE_FRAME;

	if (faulttype == VM_FAULT_READ) { 
		/*
			1. check if page is within a "valid" segment, if not EFAULT
			2. check if page is allocated, if not allocate new page (getppages or alloc_ppages) and add it to page table
			3. Bring page into physically memory, and put it into TLB 
		*/
		int err = check_readable_segment(as, actual_address);
		if (err != 0) {
			return err;
		}

		struct page_table_ * curr_pte = get_page_table_entry(as, faultaddress);
		
		if (curr_pte != NULL) {
			// Add the page as a TLB entry
			int err = create_tlb_entry(faultaddress, faulttype, curr_pte);
			if (err != 0) {
				return err;
			}
		} else {
			// Allocate new page
			struct page_table_ * new_pte = kmalloc(sizeof(struct page_table_));
			if (new_pte == NULL) {
				return ENOMEM;
			}

			int err = create_pte_entry(faultaddress, as, (unsigned long) 1, new_pte);
			if (err != 0) {
				return err;
			}

			// Add the page as a TLB entry
			err = create_tlb_entry(faultaddress, faulttype, new_pte);
			if (err != 0) {
				return err;
			}
		}

	} else if (faulttype == VM_FAULT_WRITE) {
		int err = check_writable_segment(as, actual_address);
		if (err != 0) {
			return err;
		}

		struct page_table_ * curr_pte = get_page_table_entry(as, faultaddress);

		if (curr_pte != NULL) {
			// Add the page as a TLB entry
			int err = create_tlb_entry(faultaddress, faulttype, curr_pte);
			if (err != 0) {
				return err;
			}
		} else {
			// Allocate new page
			struct page_table_ * new_pte = kmalloc(sizeof(struct page_table_));
			if (new_pte == NULL) {
				return ENOMEM;
			}
			
			int err = create_pte_entry(faultaddress, as, (unsigned long) 1, new_pte);
			if (err != 0) {
				return err;
			}

			// Add the page as a TLB entry
			err = create_tlb_entry(faultaddress, faulttype, new_pte);
			if (err != 0) {
				return err;
			}
		}

	} else if (faulttype == VM_FAULT_READONLY) {
		/* We always create pages read-write, so we can't get this */
		int err = check_writable_segment(as, actual_address);
		if (err != 0) {
			// The user is not allowed to write to the page
			return err;
		}

		// The user is actually allowed to write to the page
		struct page_table_ * curr_pte = get_page_table_entry(as, faultaddress);
		/*
			We get the TLB entry, set the dirty bit, write the tlb entry back
			Change the physical page's state to PAGE_STATE_DIRTY
		*/
		int spl = splhigh();
		uint32_t entry_hi = faultaddress | (curproc->p_process_id << 6);
		int index = tlb_probe(entry_hi, (uint32_t) NULL); //entry_lo is not actually used
		uint32_t entry_lo = curr_pte->physical_page_number | (6 << 8); // sets nocache, dirty, valid, global (0110)
		tlb_write(entry_hi, entry_lo, index);
		splx(spl);
	} else {
		return EINVAL;
	}

	//Success
	return 0;
}

/*
 * Check if the fault address is in a valid readable segment
 */
int 
check_readable_segment(struct addrspace *as, vaddr_t faultaddress) 
{
	vaddr_t actual_address = faultaddress;
	faultaddress &= PAGE_FRAME;
	bool within_segment = false;

	// Check if the fault address is within a segment of the segment array
	size_t num_segments = segmentarray_num(&as->as_segment_array);
	for (unsigned i = 0; i < num_segments; i++) {
		struct segment * candidate_segment = segmentarray_get(&as->as_segment_array, i);
		if (faultaddress >= candidate_segment->segment_start && faultaddress < candidate_segment->segment_end) {
			//fault address is within a segment
			if (candidate_segment->readable == 0) {
				return EFAULT;
			} else {
				within_segment = true;
				break;
			}
		}
	}

	if (!within_segment) {
		// fault address could not be found within a segment, check the stack
		bool within_stack = check_within_stack(as, actual_address);
		if (!within_stack) {
			return EFAULT;
		}	
	}

	//Success
	return 0;
}

/*
 * Check if the fault address is in a valid writable segment
 */
int 
check_writable_segment(struct addrspace *as, vaddr_t faultaddress) 
{
	vaddr_t actual_address = faultaddress;
	faultaddress &= PAGE_FRAME;
	spinlock_acquire(&as->as_segmentarray_spinlock);
	bool within_segment = false;

	// Check if the fault address is within a segment of the segment array
	size_t num_segments = segmentarray_num(&as->as_segment_array);
	for (unsigned i = 0; i < num_segments; i++) {
		struct segment * candidate_segment = segmentarray_get(&as->as_segment_array, i);
		if (faultaddress >= candidate_segment->segment_start && faultaddress < candidate_segment->segment_end) {
			//fault address is within a segment
			if (candidate_segment->writeable == 0) {
				spinlock_release(&as->as_segmentarray_spinlock);
				return EFAULT;
			} else {
				within_segment = true;
				break;
			}
		}
	}
	spinlock_release(&as->as_segmentarray_spinlock);
	
	if (!within_segment) {
		// fault address could not be found within a segment, check the stack
		bool within_stack = check_within_stack(as, actual_address);
		if (!within_stack) {
			return EFAULT;
		}	
	}

	//Success
	return 0;
}

/*
 * Check if the fault address is in the stack segment
 */
bool
check_within_stack(struct addrspace *as, vaddr_t faultaddress) 
{
	/* Get heap segment */
	spinlock_acquire(&as->as_segmentarray_spinlock);
	struct segment *heap_segment = segmentarray_get(&as->as_segment_array, 0);

	vaddr_t heap_top = heap_segment->segment_end;

	if (faultaddress >= as->as_stack_top && faultaddress <= as->as_stack_base) {
		/* In the stack page */
		spinlock_release(&as->as_segmentarray_spinlock);
		return true;
	}

	/* Check if there is enough space between heap and stack to expand stack */
	if (heap_top >= as->as_stack_top - PAGE_SIZE) {
		/* Not enough space */
		spinlock_release(&as->as_segmentarray_spinlock);
		return false;
	} else {
		as->as_stack_top -= PAGE_SIZE;
		spinlock_release(&as->as_segmentarray_spinlock);
		return true;
	}
}

/*
 * Given the address space and fault address, gets the corresponding page table entry
 */
struct page_table_ * 
get_page_table_entry(struct addrspace *as, vaddr_t faultaddress) 
{
	bool acquired = false;
	if (!spinlock_do_i_hold(&as->as_pt_array_spinlock)) {
		spinlock_acquire(&as->as_pt_array_spinlock);
		acquired = true;
	}

	// Check to see if fault address is found within a page table entry in the page array
	struct page_table_ * pte;
	size_t num_page_table_entries = page_table_array_num(&as->as_pt_array);
	for (unsigned i = 0; i < num_page_table_entries; i++) {
		pte = page_table_array_get(&as->as_pt_array, i);
		if (faultaddress == pte->virtual_page_number) {
			// Success, page table entry found
			if (acquired) spinlock_release(&as->as_pt_array_spinlock);
			return pte;
		}
	}
	if (acquired) spinlock_release(&as->as_pt_array_spinlock);
	return NULL;
}

/*
 * Given the fault address and fault type, creates the corresponding tlb entry
 */
int
create_tlb_entry(vaddr_t faultaddress, int faulttype, struct page_table_ *pte) 
{
	int spl = splhigh();
	uint32_t entry_hi = faultaddress | (curproc->p_process_id << 6);
	uint32_t entry_lo;

	// Creates the entry_lo register entry
	if (faulttype == VM_FAULT_READ || faulttype == VM_FAULT_WRITE) {
		entry_lo = pte->physical_page_number | (6 << 8); // sets nocache, dirty, valid, global (0110)
	} else if (faulttype == VM_FAULT_READONLY) {
		splx(spl);
		return EINVAL;
	} else {
		splx(spl);
		return EINVAL;
	}

	tlb_random(entry_hi, entry_lo);
	splx(spl);
	
	// Success
	return 0;
}

/*
 * Creates a new virtual/physical page table entry and adds it into the page table
 */
int
create_pte_entry(vaddr_t faultaddress, struct addrspace *as, unsigned long npages, struct page_table_ *new_pte)
{
	bool acquired = false;
	if (!spinlock_do_i_hold(&as->as_pt_array_spinlock)) {
		spinlock_acquire(&as->as_pt_array_spinlock);
		acquired = true;
	}
	paddr_t ppage = getppages(npages);
	
	if (ppage == 0) {
		if (acquired) spinlock_release(&as->as_pt_array_spinlock);
		return ENOMEM;
	}

	KASSERT(ppage == (ppage & PAGE_FRAME));

	// Create a new page table entry
	if (new_pte == NULL) {
		if (acquired) spinlock_release(&as->as_pt_array_spinlock);
		return EFAULT;
	}

	new_pte->virtual_page_number = faultaddress;
	new_pte->physical_page_number = ppage;

	// Update the page table with new virtual/physical mapping
	unsigned * index_ret = NULL;
	page_table_array_add(&as->as_pt_array, new_pte, index_ret);

	//Success
	if (acquired) spinlock_release(&as->as_pt_array_spinlock);
	
	return 0;
}