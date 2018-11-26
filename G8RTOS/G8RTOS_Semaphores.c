/*
 * G8RTOS_Semaphores.c
 */

/*********************************************** Dependencies and Externs *************************************************************/

#include <stdint.h>
#include "msp.h"
#include "G8RTOS_Semaphores.h"
#include "G8RTOS_CriticalSection.h"
#include "G8RTOS_Structures.h"
#include "G8RTOS_Scheduler.h"

/*********************************************** Dependencies and Externs *************************************************************/


/*********************************************** Public Functions *********************************************************************/

/*
 * Initializes a semaphore to a given value
 * Param "s": Pointer to semaphore
 * Param "value": Value to initialize semaphore to
 * THIS IS A CRITICAL SECTION
 */
void G8RTOS_InitSemaphore(semaphore_t *s, int32_t value)
{
    int primask;
    //start a critical section
    primask = StartCriticalSection();
    //init the semaphore to a given value
    (*s) = value;
    //end the critical section
    EndCriticalSection(primask);
	/* Implement this */
}

/*
 * No longer waits for semaphore
 *  - Decrements semaphore
 *  - Blocks thread is sempahore is unavalible
 * Param "s": Pointer to semaphore to wait on
 * THIS IS A CRITICAL SECTION
 */
void G8RTOS_WaitSemaphore(semaphore_t *s)
{
    int primask;
    //start a critical section
    primask = StartCriticalSection();
    //wait until resources are available

    (*s)--;

    //block the thread if semaphore not available
    if((*s) < 0){
        CurrentlyRunningThread->blocked = s;
        StartContextSwitch();
    }

    //end critical section
    EndCriticalSection(primask);

	/* Implement this */
}

/*
 * Signals the completion of the usage of a semaphore
 *  - Increments the semaphore value by 1
 *  - Unblocks any threads waiting on that semaphore
 * Param "s": Pointer to semaphore to be signaled
 * THIS IS A CRITICAL SECTION
 */
void G8RTOS_SignalSemaphore(semaphore_t *s)
{
    int primask;
    //start critical section
    primask = StartCriticalSection();
    //give up resource
    (*s)++;

    //unblock a thread if we were out of resources
    if((*s) <= 0){
        tcb_t *pt = (tcb_t *)&CurrentlyRunningThread->nextTCB;
        while(pt->blocked != s){
            pt = pt->nextTCB;
        }
        pt->blocked = 0;
    }
    //end critical section
    EndCriticalSection(primask);
	/* Implement this */
}

/*********************************************** Public Functions *********************************************************************/


