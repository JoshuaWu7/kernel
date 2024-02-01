
#ifndef _GENERIC_VM_H_
#define _GENERIC_VM_H_

#include <addrspace.h>
#include <vm.h>
#include <limits.h>
#include <array.h>


/********** Definitions ************/


/* 
 * Physical Page Allocation
 */
#define PAGE_ALLOCATED 1
#define PAGE_FREE 0


/* 
 * VM Bootstrap Completion Status
 */
#define COMPLETE 1
#define NOT_COMPLETE 0


/* 
 * Segment Flags
 */
#define READABLE 4
#define WRITEABLE 2
#define EXECUTABLE 0
#define NOT_WRITEABLE 0



/************** Macros ****************/

/* 
 * Address/Page Conversion Macros
 */
#define PADDR_TO_PPAGE(paddr) (paddr & PAGE_FRAME)
#define VADDR_TO_VPAGE(vaddr)  (vaddr / PAGE_SIZE)
#define KVADDR_TO_PADDR(vaddr) ((vaddr)-MIPS_KSEG0)




/************** Data Structures **************/

/* 
 * Segment Structure
 * Track the start and end addresses of the segment as well as permissions
 */
struct segment {
    vaddr_t segment_start;
    vaddr_t segment_end;
    int readable;
    int writeable;
    int executable;
    int originally_writeable;
};

/* Define dynamic array type for segments */
#ifndef SEGMENTINLINE
#define SEGMENTINLINE INLINE
#endif

DECLARRAY(segment, SEGMENTINLINE);
DEFARRAY(segment, SEGMENTINLINE);


/* 
 * Page Table Entry
 * Named page_table_ since "array" will get appended to this for the Page Table Array
 */
struct page_table_ {
    vaddr_t virtual_page_number;
    paddr_t physical_page_number;
};

/* Define dynamic array type for page table entries */
#ifndef PAGETABLEINLINE
#define PAGETABLEINLINE INLINE
#endif

DECLARRAY(page_table_, PAGETABLEINLINE);
DEFARRAY(page_table_, PAGETABLEINLINE);




/* 
 * Core Map entry
 */
struct core_map_entry {
    bool page_allocated;                /* Allocated or not*/
    unsigned long num_pages_track;      /* Number of pages this "first page" tracks */
};

/* 
 * Core Map array 
 * Size is calculated in init_core_map
 */
struct core_map_entry *core_map;  /* Array of Core Map Entries */




/************** Functions **************/


/* Initialize core map */
int core_map_init(void);

/* Get current process's address space */
struct addrspace *curproc_getas(void);

/* Initialization function */
void vm_bootstrap(void);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages);
paddr_t getppages(unsigned long npages);
void free_kpages(vaddr_t addr);
void free_page(paddr_t addr);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all(void);
void vm_tlbshootdown(const struct tlbshootdown *);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* vm_fault helpers */
int vm_fault_helper(int faulttype, vaddr_t faultaddress, struct addrspace *as); 
int check_readable_segment(struct addrspace *as, vaddr_t faultaddress);
struct page_table_ *get_page_table_entry(struct addrspace *as, vaddr_t faultaddress);
int create_tlb_entry(vaddr_t faultaddress, int faulttype, struct page_table_ *pte);
int create_pte_entry(vaddr_t faultaddress, struct addrspace *as, unsigned long npages, struct page_table_ *new_pte);
int check_writable_segment(struct addrspace *as, vaddr_t faultaddress);
bool check_within_stack(struct addrspace *as, vaddr_t faultaddress);

#endif /* _GENERIC_VM_H_ */
