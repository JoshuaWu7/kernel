/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define N_DANDELION_MARIGOLD 2
#define NROPES 16
static volatile int ropes_left = NROPES;

/* Data structures for rope mappings */

/* Implement this! */

struct Rope {
    volatile bool is_attached;
    struct lock *lock;
};

struct Hook {
    volatile int rope_index;
};

struct Stake {
    volatile int rope_index;
    struct lock *lock;
};

struct Hook* hooks[NROPES];
struct Rope* ropes[NROPES];
struct Stake* stakes[NROPES];


/* Synchronization primitives */

/* Implement this! */

struct lock* rope_counter_lock;

struct semaphore* exit_sem;

struct cv* main_cv;
struct lock* main_lock;

/* Function Prototypes */
struct Rope* rope_create(void);
void rope_destroy(struct Rope* rope);
struct Hook* hook_create(int rope_index);
void hook_destroy(struct Hook* hook);
struct Stake* stake_create(int rope_index);
void stake_destroy(struct Stake* stake);
static void setup();
static void teardown();
static void remove_hook(int hook_index);
static void remove_stake(int stake_index);

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?

    Design:
    We represented the problem using three structs: Rope, Hook, and Stake. Rope, Hook, and Stake are each stored in an array,
    where each struct can be identified by the index in the array. 
    
    Invariants:
    - A hook will always be mapped to the same rope until it is severed, so the 1-to-1 intialization between the hook and rope will stay the same. 
    - Access to the rope can only be made through the hook or the stake, each having an antribute rope_index, which points to correct rope by indexing the rope array. 

    Locking Protocols:
    - To ensure mutual exclusion to reading and writing the rope and stake, we add a lock to the Rope and Stake struct. If a thread wants to sever a rope, we must acquire the rope lock
    before the flag can be changed. Similarly, if we want to check if a rope is severed, a thread must acquire the rope lock.
    - When FlowerKiller tries to swap the rope, the thread must acquire the stake lock to prevent race conditions when two threads want to swap the same stake with another stake. 
    In addition, we ensure that a thread must lock a stake from low to high index ordering and must not be equal to each other. We can prevent deadlocks from occurring between multiple 
    FlowerKiller threads by ensuring that the sequence that locks are acquired stay the same.
    - I surrounded ropes_left with a rope_counter_lock to prevent race conditions when multiple threads are decrementing the ropes_left value.

    Exit Conditions:
    - To check when Dandelion, Marigold, and FlowerKiller are done, we can use a semaphore to keep track of how many threads are "alive". For each thread, we will call V() to increment
    the counter when a thread is created. In our Balloon thread, we call P() to decrement the counter when the thread is destroyed. When the counter is 0, we will block on P() until the counter is 1, since
    we want to wait until a thread is created. This implementation is similar to a thread join becuase the main function needs to wait until the thread is done before "joining" all threads.
    - I used condition variables on ropes_left to exit out of the balloon thread and return to the airballoon main thread. The airballoon thread will be blocked until the balloon thread 
    signals the main airballoon thread is finished via the cv_signal function. 
 */

/*
    Creates a rope 
    @return Pointer to newly created rope
*/
struct Rope* rope_create() {
    struct Rope* rope;
    rope = kmalloc(sizeof(struct Rope));
    if (rope == NULL) {
        return NULL;
    }
    rope->lock = lock_create("Rope Lock");
    rope->is_attached = true;
    return rope;
}

/*
    Destroys a rope, frees all memory associated to the rope
    @param Pointer to rope to be destroyed
*/
void rope_destroy(struct Rope* rope) {
    if (rope == NULL) {
        return;
    }
    lock_destroy(rope->lock);
    kfree(rope);
}

/*
    Creates a hook
    @param rope_index that the hook is attached to
    @return Pointer to newly created hook
*/
struct Hook* hook_create(int rope_index) {
    struct Hook* hook;
    hook = kmalloc(sizeof(struct Hook));
    if (hook == NULL) {
        return NULL;
    }
    hook->rope_index = rope_index;
    return hook;
}

/*
    Destroys a hook, frees all memory associated to the hook
    @param Pointer to hook to be destroyed
*/
void hook_destroy(struct Hook* hook) {
    if (hook == NULL) {
        return;
    }
    kfree(hook);
}

/*
    Creates a stake
    @param rope_index that the stake is attached to
    @return Pointer to newly created stake
*/
struct Stake* stake_create(int rope_index) {
    struct Stake* stake;
    stake = kmalloc(sizeof(struct Stake));
    if (stake == NULL) {
        return NULL;
    }
    stake->lock = lock_create("Stake Lock");
    stake->rope_index = rope_index;
    return stake;
}

/*
    Destroys a stake, frees all memory associated to the stake
    @param Pointer to stake to be destroyed
*/
void stake_destroy(struct Stake* stake) {
    if (stake == NULL) {
        return;
    }
    lock_destroy(stake->lock);
    kfree(stake);
}

/*
    Helper Function: sets up all rope/hook/stake data structure and initializes synchronization primitives
*/
static void setup() {
    for (int index = 0; index < NROPES; index++) {
        ropes[index] = rope_create();
        hooks[index] = hook_create(index);
        stakes[index] = stake_create(index);
    }

    rope_counter_lock = lock_create("Rope Counter Lock");
    exit_sem = sem_create("exit_sem", 0);
    main_cv = cv_create("Main Thread CV");
    main_lock = lock_create("Main Thread Lock");
}


/*
    Helper Function: tears down all rope/hook/stake data structure and deallocates all synchronization primitives
*/
static void teardown() {
    for (int index = 0; index < NROPES; index++) {
        rope_destroy(ropes[index]);
        stake_destroy(stakes[index]);
        hook_destroy(hooks[index]);
    }

    lock_destroy(rope_counter_lock);
    cv_destroy(main_cv);
    lock_destroy(main_lock);
}

/*
    Helper Function to remove the hook from the rope
    Function is not synchronized on the rope
*/
static void remove_hook(int hook_index) {
    struct Hook *current_hook = hooks[hook_index];
    KASSERT(current_hook != NULL);
    int rope_index = current_hook->rope_index;
    KASSERT(ropes[rope_index] != NULL);

    if (ropes[rope_index]->is_attached == true) {
        ropes[rope_index]->is_attached = false;

        lock_acquire(rope_counter_lock);
        ropes_left--;
        lock_release(rope_counter_lock);

        kprintf("Dandelion severed rope %d\n", rope_index);
    }
}

/*
    Helper Function to remove the stake from the rope
    Function is not syncrhonized on the rope and stake
*/
static void remove_stake(int stake_index) {
    struct Stake *current_stake = stakes[stake_index];
    KASSERT(current_stake != NULL);
    int rope_index = current_stake->rope_index;
    KASSERT(ropes[rope_index] != NULL);

    if (ropes[rope_index]->is_attached == true) {
        ropes[rope_index]->is_attached = false;
        
        lock_acquire(rope_counter_lock);
        ropes_left--;
        lock_release(rope_counter_lock);

        kprintf("Marigold severed rope %d from stake %d\n", rope_index, stake_index);
    }
}

/*
    Helper function to switch the ropes of two stake indices
    @param stake_index1 stake index to switch with
    @param stake_index2 stake index to switch with
    
    Function is not synchronized on the rope and stake
    We assume that stake_index1 and stake_index2 are connected to ropes that are not severed
*/
static void switch_rope(int stake_index1, int stake_index2) {
    struct Stake *stake1 = stakes[stake_index1];
    KASSERT(stake1 != NULL);
    struct Stake *stake2 = stakes[stake_index2];
    KASSERT(stake2 != NULL);

    int rope_index1 = stake1->rope_index;
    int rope_index2 = stake2->rope_index;

    int rope_index_temp = rope_index1;
    stake1->rope_index = rope_index2;
    stake2->rope_index = rope_index_temp;

    kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope_index1, stake_index1, stake_index2);
    kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope_index2, stake_index2, stake_index1);
}

/*
    Dandelion's run thread
*/
static
void
dandelion(void *p, unsigned long arg)
{
    //Note: Dandelion can only access the rope through the hook
    (void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n");

	/* Implement this function */
    while (ropes_left > 0) {
        int hook_index = random() % NROPES;
        int rope_index = hooks[hook_index]->rope_index;

        lock_acquire(ropes[rope_index]->lock);
        remove_hook(hook_index);
        lock_release(ropes[rope_index]->lock);

        thread_yield();
    }

    kprintf("Dandelion thread done\n");
    V(exit_sem);
}

/*
    Marigold's run thread
*/
static
void
marigold(void *p, unsigned long arg)
{
	//Note: Marigold can only access the rope through the stake
    (void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");

	/* Implement this function */
    while (ropes_left > 0) {
        int stake_index = random() % NROPES;

        lock_acquire(stakes[stake_index]->lock);
        int rope_index = stakes[stake_index]->rope_index;

        lock_acquire(ropes[rope_index]->lock);
        remove_stake(stake_index);
        lock_release(ropes[rope_index]->lock);
        lock_release(stakes[stake_index]->lock);

        thread_yield();
    }

    kprintf("Marigold thread done\n");
    V(exit_sem);
}

/*
    FlowerKiller's run thread
*/
static
void
flowerkiller(void *p, unsigned long arg)
{
	//Note: FlowerKillder switches the stakes connected to ropes
    (void)p;
	(void)arg;

	kprintf("Lord FlowerKiller thread starting\n");
    int stake_index_lo;
    int stake_index_hi;

	/* Implement this function */   
    do {
        stake_index_lo = random() % NROPES;
        stake_index_hi = random() % NROPES;

        //To prevent deadlock: acquire the lo then hi index lock
        if (stake_index_lo < stake_index_hi) {
            lock_acquire(stakes[stake_index_lo]->lock);
            lock_acquire(stakes[stake_index_hi]->lock);

            int rope_index1 = stakes[stake_index_lo]->rope_index;
            int rope_index2 = stakes[stake_index_hi]->rope_index;
            lock_acquire(ropes[rope_index1]->lock);
            lock_acquire(ropes[rope_index2]->lock);

            if (ropes[rope_index1]->is_attached == true && ropes[rope_index2]->is_attached == true) {
                switch_rope(stake_index_lo, stake_index_hi);
                
                lock_release(ropes[rope_index1]->lock);
                lock_release(ropes[rope_index2]->lock);

                lock_release(stakes[stake_index_lo]->lock);
                lock_release(stakes[stake_index_hi]->lock);
                thread_yield();
            } else {
                lock_release(ropes[rope_index1]->lock);
                lock_release(ropes[rope_index2]->lock);

                lock_release(stakes[stake_index_lo]->lock);
                lock_release(stakes[stake_index_hi]->lock);
            }
        }
        //Note: there needs to be at lease two ropes left to do a swap
    } while (ropes_left > 1);

    kprintf("Lord FlowerKiller thread done\n");
    V(exit_sem);
}

/*
    Balloon's run thread
*/
static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	/* Implement this function */
    for (int i = 0; i < N_LORD_FLOWERKILLER + N_DANDELION_MARIGOLD; i++) {
        P(exit_sem);
    }

    lock_acquire(main_lock);

    kprintf("Balloon freed and Prince Dandelion escapes!\n");
    kprintf("Balloon thread done\n");

    // let the airballoon main thread know that all threads have been finished!
    cv_signal(main_cv, main_lock);
    lock_release(main_lock);
}


// Change this function as necessary
int
airballoon(int nargs, char **args)
{

	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

    ropes_left = NROPES;

    setup();

    lock_acquire(main_lock);

	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

    // Wait until the balloon thread is done
    while (ropes_left > 0) {
        cv_wait(main_cv, main_lock);
    }
    lock_release(main_lock);

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
    // Deallocate all resources
    teardown();
    kprintf("Main thread done\n");
	return 0;
}
