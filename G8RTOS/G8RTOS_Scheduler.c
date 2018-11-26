/*
 * G8RTOS_Scheduler.c
 */

/*********************************************** Dependencies and Externs *************************************************************/

#include "msp.h"
#include "BSP.h"
#include "driverlib.h"
#include "G8RTOS_Structures.h"
#include "G8RTOS_CriticalSection.h"
#include "LCDLib.h"
#include "G8RTOS_IPC.h"
//#include "Threads.h"

#define ICSR (*((volatile unsigned int*)(0xe000ed04)))

/*
 * G8RTOS_Start exists in asm
 */
extern void G8RTOS_Start();

/* System Core Clock From system_msp432p401r.c */
extern uint32_t SystemCoreClock;

/*
 * Pointer to the currently running Thread Control Block
 */
extern tcb_t * CurrentlyRunningThread;

/*********************************************** Dependencies and Externs *************************************************************/


/*********************************************** Defines ******************************************************************************/

/* Status Register with the Thumb-bit Set */
#define THUMBBIT 0x01000000

/*********************************************** Defines ******************************************************************************/


/*********************************************** Data Structures Used *****************************************************************/

/* Thread Control Blocks
 *	- An array of thread control blocks to hold pertinent information for each thread
 */
static tcb_t threadControlBlocks[MAX_THREADS];

//A number used to init each thread's id
static uint16_t IDCounter;

/* Thread Stacks
 *	- An array of arrays that will act as invdividual stacks for each thread
 */
static int32_t threadStacks[MAX_THREADS][STACKSIZE];

/* Periodic Event Threads
 * - An array of periodic events to hold pertinent information for each thread
 */
static struct ptcb_t Pthread[MAXPTHREADS];

/*********************************************** Data Structures Used *****************************************************************/


/*********************************************** Private Variables ********************************************************************/

/*
 * Current Number of Threads currently in the scheduler
 */
static uint32_t NumberOfThreads;

/*
 * Current Number of Periodic Threads currently in the scheduler
 */
static uint32_t NumberOfPthreads;

/*********************************************** Private Variables ********************************************************************/


/*********************************************** Private Functions ********************************************************************/

/*
 * Initializes the Systick and Systick Interrupt
 * The Systick interrupt will be responsible for starting a context switch between threads
 * Param "numCycles": Number of cycles for each systick interrupt
 */
static void InitSysTick(uint32_t numCycles)
{
    //Set the period for SysTick and enable its interrupt.
    SysTick_Config(numCycles);
    SysTick_enableInterrupt();
}

/*
 * Chooses the next thread to run.
 * Lab 4 Scheduling Algorithm:
 * 	- Simple Round Robin: Choose the next running thread with highest priority
 * 	- Check for sleeping and blocked threads
 */
void G8RTOS_Scheduler()
{
    //counter for current max priority for below loop
    uint16_t currentMaxPriority = 256;

    //tcb for iterating through the threads
    tcb_t * tempNextThread;
    tempNextThread = CurrentlyRunningThread->nextTCB;

    //find the first thread thread that is not blocked or asleep and has the highest priority
    while(tempNextThread != CurrentlyRunningThread){
        if(tempNextThread->blocked == 0 && tempNextThread->Asleep == 0 && tempNextThread->priority < currentMaxPriority){

            //set the next thread, update max priority
            CurrentlyRunningThread = tempNextThread;
            currentMaxPriority = tempNextThread->priority;
        }
        tempNextThread = tempNextThread->nextTCB;
    }

}

/*
 * Starts a Context Switch
 */
void StartContextSwitch(){
    //Set a pending PendSV interrupt
    ICSR |= 0x10000000;
}

/*
 * SysTick Handler
 * The Systick Handler now will increment the system time,
 * and be responsible for handling sleeping and periodic threads
 */
void SysTick_Handler()
{
    //Increment system time
    SystemTime++;

    //Check if a pthread needs to be run
    int i;
    for(i=0; i < NumberOfPthreads; i++){
        if(Pthread[i].ExecuteTime == SystemTime){
            Pthread[i].ExecuteTime = Pthread[i].Period+SystemTime;
            Pthread[i].Handler();
        }
    }

    //check if we need to wake up a thread
    tcb_t * temp = CurrentlyRunningThread->nextTCB;
    while(temp != CurrentlyRunningThread){
        if(temp->Sleep_Count <= SystemTime){
            temp->Asleep = 0;
        }
        temp = temp->nextTCB;
    }

    //Do the context switch
    StartContextSwitch();
}

/*********************************************** Private Functions ********************************************************************/


/*********************************************** Public Variables *********************************************************************/

/* Holds the current time for the whole System */
uint32_t SystemTime;

/*********************************************** Public Variables *********************************************************************/


/*********************************************** Public Functions *********************************************************************/

/*
 * Sets variables to an initial state (system time and number of threads)
 * Enables board for highest speed clock and disables watchdog
 * Initializes the LCD screen
 * Initializes all semaphores
 * Initializes all FIFOs
 */
void G8RTOS_Init()
{
    //Initialize the board
    BSP_InitBoard();

    //Initialize the LCD screen
    LCD_Init(1);

    //create semaphores for the ADC and LCD Screen
    //G8RTOS_InitSemaphore(&adc_s, 1);
    //G8RTOS_InitSemaphore(&lcd_s, 1);

    //create the FIFOs
    G8RTOS_InitFIFO(0);
    G8RTOS_InitFIFO(1);

    //Reset number of threads, pthreads, and IDCounter
    NumberOfThreads = 0;
    NumberOfPthreads = 0;
    IDCounter = 0;

    //Create a new custom vector table in writable memory
    uint32_t newVTORTable = 0x20000000;
    memcpy((uint32_t *)newVTORTable, (uint32_t *)SCB->VTOR, 57*4);  // 57 interrupt vectors to copy
    SCB->VTOR = newVTORTable;
}

/*
 * Starts G8RTOS Scheduler
 * 	- Initializes the Systick
 * 	- Sets Context to first thread
 * Returns: Error Code for starting scheduler. This will only return if the scheduler fails
 */
sched_ErrCode_t G8RTOS_Launch()
{
    //set the first running thread
    CurrentlyRunningThread = &threadControlBlocks[0];

    //Set interrupts to the lowest priority
    MAP_Interrupt_setPriority(14, 0xe0);
    MAP_Interrupt_setPriority(15, 0xe0);

    //Init the Systick to be 1ms
    InitSysTick(ClockSys_GetSysFreq()/1000);

    //Set up the sp and start the first thread
    G8RTOS_Start();


    return NO_ERROR;

}


/*
 * Returns the threadID of the currently running thread
 */
uint32_t G8RTOS_GetThreadId(){
    return CurrentlyRunningThread->threadID;
}

/*
 * Adds threads to G8RTOS Scheduler
 * 	- Replaces a dead thread
 * 	- Initializes the thread control block for the provided thread
 * 	- Initializes the stack for the provided thread to hold a "fake context"
 * 	- Sets stack tcb stack pointer to top of thread stack
 * 	- Sets up the next and previous tcb pointers in a round robin fashion
 * Parameters "threadToAdd": Void-Void Function to add as preemptable main thread
 *          "priority": Priority of thread being made
 *          "name": Name of thread
 * Returns: Error code for adding threads
 */
sched_ErrCode_t G8RTOS_AddThread(void (*threadToAdd)(void), uint8_t priority, char * name)
{

    //start a critical section
    int primask;
    primask = StartCriticalSection();

    //check if space in the scheduler
    if(NumberOfThreads >= MAX_THREADS){
        EndCriticalSection(primask);
        return THREAD_LIMIT_REACHED;
    }


    //create new TCB for new thread
    tcb_t newTCB;

    //iterate through and find the first dead thread to replace
    int j = 0;
    tcb_t * tcbToInitialize = &threadControlBlocks[j];
    while(tcbToInitialize->isAlive == 1){
        j++;

        //If we have failed to find a dead thread,
        //leave critical section and return an error
        if(j == MAX_THREADS){
            EndCriticalSection(primask);
            return THREADS_INCORRECTLY_ALIVE;
        }
        tcbToInitialize = &threadControlBlocks[j];
    }


    //Initialize the fake context for the new thread
    int i;
    //r0-r12
    for(i = 0; i < 14; i++){
        threadStacks[j][STACKSIZE - 16 + i] = i*(j+1);
    }

    //set PC to function pointer
    threadStacks[j][STACKSIZE - 2] = (int32_t)threadToAdd;
    //Set psr to have thumb bit set
    threadStacks[j][STACKSIZE - 1] = THUMBBIT;

    //Set newTCB's stack pointer to top of the stack
    newTCB.StackP = &threadStacks[j][STACKSIZE-16];

    //Set the priority and name of this new thread
    newTCB.priority = priority;
    newTCB.threadName = name;

    //Create unique threadID
    newTCB.threadID = ((IDCounter++)<<16) | tcbToInitialize->threadID;

    //Wake up and unblock the new thread
    newTCB.Asleep = 0;
    newTCB.blocked = 0;
    //mark this thread as alive
    newTCB.isAlive = 1;

    //fit it into our list of threads, at the back
    //j is the index of the dead thread being replaced
    if(j ==  0){
        newTCB.prevTCB = &threadControlBlocks[0];
        newTCB.nextTCB = &threadControlBlocks[0];
        threadControlBlocks[j] = newTCB;
    } else {
        newTCB.prevTCB = threadControlBlocks[0].prevTCB;
        newTCB.nextTCB = &threadControlBlocks[0];
        threadControlBlocks[j] = newTCB;
        threadControlBlocks[0].prevTCB->nextTCB = &threadControlBlocks[j];
        threadControlBlocks[0].prevTCB = &threadControlBlocks[j];
    }


    //increment our number of threads
    NumberOfThreads++;

    //End the critical section and return
    EndCriticalSection(primask);
    return NO_ERROR;
}


/*
 * Adds periodic threads to G8RTOS Scheduler
 * Function will initialize a periodic event struct to represent event.
 * The struct will be added to a linked list of periodic events
 * Parameter Pthread To Add: void-void function for P thread handler
 * Parameter period: period of P thread to add
 * Returns: Error code for adding threads
 */
sched_ErrCode_t G8RTOS_AddPeriodicEvent(void (*PthreadToAdd)(void), uint32_t period)
{
    //Start a Critical Section
    int primask;
    primask = StartCriticalSection();

    //check if space in the scheduler
    if(NumberOfPthreads >= MAXPTHREADS){
        EndCriticalSection(primask);
        return THREAD_LIMIT_REACHED;
    }

    //create new PTCB for new pthread
    ptcb_t newPTCB;

    //Init the members
    newPTCB.Period = period;
    newPTCB.Handler = PthreadToAdd;
    newPTCB.ExecuteTime = NumberOfPthreads+1;
    newPTCB.CurrentTime = 0;

    //fit it into our list of pthreads, at the back
    if(NumberOfPthreads == 0){
        newPTCB.prevPTCB = &Pthread[0];
        newPTCB.nextPTCB = &Pthread[0];
        Pthread[0] = newPTCB;
    }
    else {
        newPTCB.prevPTCB = &Pthread[NumberOfPthreads-1];
        newPTCB.nextPTCB = &Pthread[0];
        Pthread[NumberOfPthreads] = newPTCB;
        Pthread[NumberOfPthreads-1].nextPTCB = &Pthread[NumberOfPthreads];
        Pthread[0].prevPTCB = &Pthread[NumberOfPthreads];
    }

    //increment our number of threads
    NumberOfPthreads++;

    //End critical section and return
    EndCriticalSection(primask);
    return NO_ERROR;
}

/*
 *Adds an aperiodic event by using a custom vector table in writable memory
 */
sched_ErrCode_t G8RTOS_AddAPeriodicEvent(void (*AthreadToAdd)(void), uint8_t priority, IRQn_Type IRQn){
    //Start a critical section
    int primask;
    primask = StartCriticalSection();

    //Check that the IRQn is valid
    if(IRQn < PSS_IRQn || IRQn > PORT6_IRQn){
        EndCriticalSection(primask);
        return IRQn_INVALID;
    }
    //Check that the priority is valid
    if(priority > 6){
        EndCriticalSection(primask);
        return HWI_PRIORITY_INVALID;
    }

    //Add the vector, set its priority, and enable the interrupt
    __NVIC_SetVector(IRQn, AthreadToAdd);
    __NVIC_SetPriority(IRQn, priority);
    __NVIC_EnableIRQ(IRQn);

    //End critical section and return
    EndCriticalSection(primask);
    return NO_ERROR;
}

/*
 * Puts the current thread into a sleep state.
 *  param durationMS: Duration of sleep time in ms
 */
void sleep(uint32_t durationMS)
{
    //set the next time to sleep and then go to sleep
    CurrentlyRunningThread->Sleep_Count = durationMS+SystemTime;
    CurrentlyRunningThread->Asleep = 1;
    StartContextSwitch();
    /* Implement this */
}

/*
 * Kills thread via threadId.
 *  param threadId: ID of thread to kill
 *  returns: possible error code
 */
sched_ErrCode_t G8RTOS_KillThread(threadId_t threadId){
    //Start a critical section
    int primask;
    primask = StartCriticalSection();

    //Check if we are trying to kill the only running thread
    if(NumberOfThreads == 1){
        EndCriticalSection(primask);
        return CANNOT_KILL_LAST_THREAD;
    }

    //Iterate through and find the thread with the matching ID
    int j = 0;
    tcb_t * findThread = &threadControlBlocks[j];
    while(findThread->threadID != threadId){
        j++;

        //If no thread is found, return an error
        if(j == MAX_THREADS){
            EndCriticalSection(primask);
            return THREAD_DOES_NOT_EXIST;
        }

        findThread = &threadControlBlocks[j];
    }

    //kill the thread and update the list of running threads
    findThread->isAlive = 0;
    findThread->prevTCB->nextTCB = findThread->nextTCB;
    findThread->nextTCB->prevTCB = findThread->prevTCB;

    //Decrement the number of living threads
    NumberOfThreads--;

    //End the critical section
    EndCriticalSection(primask);

    //If we just killed the currently running thread, start the next thread
    if(findThread->threadID == CurrentlyRunningThread->threadID){
        StartContextSwitch();
    }

    //Else, return
    return NO_ERROR;
}

/*
 * Kills currently running thread.
 *  returns: possible error code
 */
sched_ErrCode_t G8RTOS_KillSelf(){
    //Start a critical section
    int primask;
    primask = StartCriticalSection();

    //if we are trying to kill the only thread, return an error
    if(NumberOfThreads == 1){
        EndCriticalSection(primask);
        return CANNOT_KILL_LAST_THREAD;
    }

    //Kill the currently running thread and update the list of running threads
    CurrentlyRunningThread->isAlive = 0;
    CurrentlyRunningThread->prevTCB->nextTCB = CurrentlyRunningThread->nextTCB;
    CurrentlyRunningThread->nextTCB->prevTCB = CurrentlyRunningThread->prevTCB;

    //Decrement number of living threads
    NumberOfThreads--;

    //End the critical section and start the next thread
    EndCriticalSection(primask);
    StartContextSwitch();
    return NO_ERROR;
}


/*********************************************** Public Functions *********************************************************************/
