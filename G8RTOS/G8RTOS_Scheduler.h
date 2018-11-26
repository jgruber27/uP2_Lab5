//#include "G8RTOS_Structures.h"
/*
 * G8RTOS_Scheduler.h
 */

#ifndef G8RTOS_SCHEDULER_H_
#define G8RTOS_SCHEDULER_H_

#include "G8RTOS.h"
#include "msp.h"


/*********************************************** Sizes and Limits *********************************************************************/
#define MAX_THREADS 25
#define MAXPTHREADS 2
#define STACKSIZE 512
#define OSINT_PRIORITY 7
/*********************************************** Sizes and Limits *********************************************************************/

typedef enum
{
    NO_ERROR                     =    0,
    THREAD_LIMIT_REACHED         =   -1,
    NO_THREADS_SCHEDULED         =   -2,
    THREADS_INCORRECTLY_ALIVE    =   -3,
    THREAD_DOES_NOT_EXIST        =   -4,
    CANNOT_KILL_LAST_THREAD      =   -5,
    IRQn_INVALID                 =   -6,
    HWI_PRIORITY_INVALID         =   -7,
    FIFO_LIMIT_REACHED           =   -8,
    BUFFER_FULL                  =   -9
} sched_ErrCode_t;

/*********************************************** Public Variables *********************************************************************/

/* Holds the current time for the whole System */
extern uint32_t SystemTime;

typedef uint32_t threadId_t;
/*********************************************** Public Variables *********************************************************************/


/*********************************************** Public Functions *********************************************************************/

/*
 * Sets variables to an initial state (system time and number of threads)
 * Enables board for highest speed clock and disables watchdog
 * Initializes the LCD screen
 * Initializes all semaphores
 * Initializes all FIFOs
 */
void G8RTOS_Init();

/*
 * Starts G8RTOS Scheduler
 * 	- Initializes Systick Timer
 * 	- Sets Context to first thread
 * Returns: Error Code for starting scheduler. This will only return if the scheduler fails
 */
sched_ErrCode_t G8RTOS_Launch();

/*
 * Starts a Context Switch
 */
void StartContextSwitch();

/*
 * Returns the threadID of the currently running thread
 */
uint32_t G8RTOS_GetThreadId();

/*
 * Adds threads to G8RTOS Scheduler
 *  - Replaces a dead thread
 *  - Initializes the thread control block for the provided thread
 *  - Initializes the stack for the provided thread to hold a "fake context"
 *  - Sets stack tcb stack pointer to top of thread stack
 *  - Sets up the next and previous tcb pointers in a round robin fashion
 * Parameters "threadToAdd": Void-Void Function to add as preemptable main thread
 *          "priority": Priority of thread being made
 *          "name": Name of thread
 * Returns: Error code for adding threads
 */
sched_ErrCode_t G8RTOS_AddThread(void (*threadToAdd)(void), uint8_t priority, char * name);


/*
 * Adds periodic threads to G8RTOS Scheduler
 * Function will initialize a periodic event struct to represent event.
 * The struct will be added to a linked list of periodic events
 * Param Pthread To Add: void-void function for P thread handler
 * Param period: period of P thread to add
 * Returns: Error code for adding threads
 */
sched_ErrCode_t G8RTOS_AddPeriodicEvent(void (*PthreadToAdd)(void), uint32_t period);

/*
 *Adds an aperiodic event by using a custom vector table in writable memory
 */
sched_ErrCode_t G8RTOS_AddAPeriodicEvent(void (*AthreadToAdd)(void), uint8_t priority, IRQn_Type IRQn);


/*
 * Puts the current thread into a sleep state.
 *  param durationMS: Duration of sleep time in ms
 */
void sleep(uint32_t durationMS);

/*
 * Kills thread via threadId.
 *  param threadId: ID of thread to kill
 *  returns: possible error code
 */
sched_ErrCode_t G8RTOS_KillThread(threadId_t threadId);

/*
 * Kills currently running thread.
 *  returns: possible error code
 */
sched_ErrCode_t G8RTOS_KillSelf();


/*********************************************** Public Functions *********************************************************************/

#endif /* G8RTOS_SCHEDULER_H_ */
