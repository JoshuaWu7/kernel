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

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
        struct semaphore *sem;

        sem = kmalloc(sizeof(struct semaphore));
        if (sem == NULL) {
                return NULL;
        }

        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL) {
                kfree(sem);
                return NULL;
        }

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
        sem->sem_count = initial_count;

        return sem;
}

void
sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

void
P(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
        while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0);
        sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

        sem->sem_count++;
        KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
        struct lock *lock;

        lock = kmalloc(sizeof(struct lock));
        if (lock == NULL) {
                return NULL;
        }

        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }

        // add stuff here as needed
        lock->lk_wchan = wchan_create(lock->lk_name);
        // if the wait channel is null, free all pointers
        if (lock->lk_wchan == NULL) {
            kfree(lock->lk_name);
            kfree(lock);
            return NULL;
        }
        
        // make sure that lock is "unlocked" and thus available upon creation        
        spinlock_init(&lock->lk_spinlock);
        lock->lk_owner = NULL;
        lock->lk_flag = false;

        KASSERT(lock->lk_owner == false);

        return lock;
}

void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);

        // add stuff here as needed
        // clean up all resources associate with the lock
        wchan_destroy(lock->lk_wchan);
        spinlock_cleanup(&lock->lk_spinlock);

        kfree(lock->lk_name);
        kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
        // Write this
        spinlock_acquire(&lock->lk_spinlock);
        
        /* 
         * Test the lock flag value:
         * True: the lock is taken, and we should put the current thread to sleep
         * False: continue since the lock is free
        */ 
        while(lock->lk_flag == true) {
            wchan_sleep(lock->lk_wchan, &lock->lk_spinlock);
            //when the thread is woken up, we check the while loop guard again to make sure the lock is free
        }
        
        KASSERT(lock->lk_flag == false);

        lock->lk_flag = true;
        lock->lk_owner = curthread;
        spinlock_release(&lock->lk_spinlock);

}

void
lock_release(struct lock *lock)
{
    spinlock_acquire(&lock->lk_spinlock);
    // Release the flag
    lock->lk_flag = false;
    lock->lk_owner = NULL;
    
    // Wakes one thread in the waitchannel in FIFO order
    wchan_wakeone(lock->lk_wchan, &lock->lk_spinlock);
    spinlock_release(&lock->lk_spinlock);
}

bool
lock_do_i_hold(struct lock *lock)
{
        // Write this
        spinlock_acquire(&lock->lk_spinlock);
        // Makes sure that the the current thread still holds the lock
        bool result = lock->lk_owner == curthread && lock->lk_flag == true;
        spinlock_release(&lock->lk_spinlock);

        return result;
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
        struct cv *cv;

        cv = kmalloc(sizeof(struct cv));
        if (cv == NULL) {
                return NULL;
        }

        cv->cv_name = kstrdup(name);
        if (cv->cv_name==NULL) {
                kfree(cv);
                return NULL;
        }

        // add stuff here as needed
        cv->cv_wchan = wchan_create(cv->cv_name);

        // free up resources if waitchannel cannot be created 
        if (cv->cv_wchan == NULL) {
            kfree(cv->cv_name);
            kfree(cv);
            return NULL;
        }

        spinlock_init(&cv->cv_spinlock);

        return cv;
}

void
cv_destroy(struct cv *cv)
{
        KASSERT(cv != NULL);

        // add stuff here as needed
        // clean up all resources associated with the condition variable
        spinlock_cleanup(&cv->cv_spinlock);
        wchan_destroy(cv->cv_wchan);
        
        kfree(cv->cv_name);
        kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
    spinlock_acquire(&cv->cv_spinlock);
    
    // release the lock and put the calling thread to sleep
    lock_release(lock);
    wchan_sleep(cv->cv_wchan, &cv->cv_spinlock);

    // when the thread wakes up, it must re-acquire the lock before returning to the caller
    // release the spinlock first to avoid deadlock situation
    spinlock_release(&cv->cv_spinlock);
    lock_acquire(lock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{    
    KASSERT(lock != NULL);
    spinlock_acquire(&cv->cv_spinlock);
    // wake up the first thread in the waitchannel
    wchan_wakeone(cv->cv_wchan, &cv->cv_spinlock);
    spinlock_release(&cv->cv_spinlock);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
    KASSERT(lock != NULL);
    spinlock_acquire(&cv->cv_spinlock);
    // wake up all threads in the waitchannel
    wchan_wakeall(cv->cv_wchan, &cv->cv_spinlock);
    spinlock_release(&cv->cv_spinlock);
}
