/*
 * G8RTOS_Structure.h
 *
 *  Created on: Jan 12, 2017
 *      Author: Raz Aloni
 */

#ifndef G8RTOS_STRUCTURES_H_
#define G8RTOS_STRUCTURES_H_

#include "G8RTOS.h"


#define MAX_NAME_LENGTH 16
/*********************************************** Data Structure Definitions ***********************************************************/


/*
 *  Thread Control Block:
 *      - Every thread has a Thread Control Block
 *      - The Thread Control Block holds information about the Thread Such as the Stack Pointer, Priority Level, and Blocked Status
 */

/* Create tcb struct here */
typedef struct tcb_t tcb_t;

struct tcb_t {
    int32_t * StackP;
    tcb_t * nextTCB;
    tcb_t * prevTCB;
    semaphore_t *blocked;
    uint32_t Sleep_Count;
    char Asleep;
    uint8_t priority;
    char isAlive;
    threadId_t threadID;
    char * threadName;
};


/*
 *  Periodic Thread Control Block:
 *      - Holds a function pointer that points to the periodic thread to be executed
 *      - Has a period in us
 *      - Holds Current time
 *      - Contains pointer to the next periodic event - linked list
 */

/* Create periodic thread struct here */
typedef struct ptcb_t ptcb_t;
struct ptcb_t{
    struct ptcb_t * nextPTCB;
    struct ptcb_t * prevPTCB;
    uint32_t CurrentTime;
    uint32_t ExecuteTime;
    uint32_t Period;
    void (*Handler)(void);
};



/*********************************************** Data Structure Definitions ***********************************************************/


/*********************************************** Public Variables *********************************************************************/

struct tcb_t * CurrentlyRunningThread;

/*********************************************** Public Variables *********************************************************************/




#endif /* G8RTOS_STRUCTURES_H_ */
