/*
 * G8RTOS_IPC.c
 *
 *  Created on: Jan 10, 2017
 *      Author: Daniel Gonzalez
 */
#include <stdint.h>
#include "msp.h"
#include "G8RTOS_IPC.h"
#include "G8RTOS_Semaphores.h"
#include "G8RTOS_Scheduler.h"

/*********************************************** Defines ******************************************************************************/

#define FIFOSIZE 16
#define MAX_NUMBER_OF_FIFOS 4



/*********************************************** Defines ******************************************************************************/


/*********************************************** Data Structures Used *****************************************************************/

/*
 * FIFO struct will hold
 *  - buffer
 *  - head
 *  - tail
 *  - lost data
 *  - current size
 *  - mutex
 */

/* Create FIFO struct here */
typedef struct FIFO_t FIFO_t;
struct FIFO_t{
    int32_t Buffer[FIFOSIZE];
    int32_t *Head;
    int32_t *Tail;
    uint32_t LostData;
    semaphore_t *CurrentSize;
    semaphore_t *Mutex;
};

/* Array of FIFOS */
static FIFO_t FIFOs[4];

/*********************************************** Data Structures Used *****************************************************************/

/*
 * Initializes FIFO Struct
 */
sched_ErrCode_t G8RTOS_InitFIFO(uint32_t FIFOIndex)
{
    //check if we're at max number of FIFOs
    if(FIFOIndex >= MAX_NUMBER_OF_FIFOS){
        return FIFO_LIMIT_REACHED;
    }

    //temporary new FIFO
    FIFO_t newFIFO;

    //Init the values of the FIFO
    newFIFO.Head = &(FIFOs[FIFOIndex].Buffer[0]);
    newFIFO.Tail = &(FIFOs[FIFOIndex].Buffer[0]);
    newFIFO.LostData = 0;
    G8RTOS_InitSemaphore(&(newFIFO.CurrentSize), 0);
    G8RTOS_InitSemaphore(&(newFIFO.Mutex), 1);

    //Add FIFO to list of FIFOs
    FIFOs[FIFOIndex] = newFIFO;

    /* Implement this */
    return NO_ERROR;
}

/*
 * Reads FIFO
 *  - Waits until CurrentSize semaphore is greater than zero
 *  - Gets data and increments the head ptr (wraps if necessary)
 * Param: "FIFOChoice": chooses which buffer we want to read from
 * Returns: uint32_t Data from FIFO
 */
uint32_t readFIFO(uint32_t FIFOChoice)
{
    //find FIFO to read from
    FIFO_t * readFIFO = &FIFOs[FIFOChoice];

    //Check if available data and mutex
    G8RTOS_WaitSemaphore(&readFIFO->CurrentSize);
    G8RTOS_WaitSemaphore(&readFIFO->Mutex);

    //Grab data
    uint32_t val = *(readFIFO->Tail);
    //Update Tail location
    if(readFIFO->Tail == &(readFIFO->Buffer[FIFOSIZE-1])){
        readFIFO->Tail = &(readFIFO->Buffer[0]);
    }
    else{
        readFIFO->Tail++;
    }

    //release mutex
    G8RTOS_SignalSemaphore(&readFIFO->Mutex);

    return val;
}

/*
 * Writes to FIFO
 *  Writes data to Tail of the buffer if the buffer is not full
 *  Increments tail (wraps if ncessary)
 *  Param "FIFOChoice": chooses which buffer we want to read from
 *        "Data': Data being put into FIFO
 *  Returns: error code for full buffer if unable to write
 */
sched_ErrCode_t writeFIFO(uint32_t FIFOChoice, uint32_t Data)
{
    //find FIFO to write to
    FIFO_t * writeFIFO = &FIFOs[FIFOChoice];

    //Check for the mutex
    G8RTOS_WaitSemaphore(&writeFIFO->Mutex);

    //check if buffer is already full
    int chk = writeFIFO->CurrentSize;
    if(chk >= FIFOSIZE-1){
        writeFIFO->LostData++;
        return BUFFER_FULL;
    }

    //write data to buffer
    *(writeFIFO->Head) = Data;

    //update head
    if(writeFIFO->Head == &(writeFIFO->Buffer[FIFOSIZE-1])){
        writeFIFO->Head = &(writeFIFO->Buffer[0]);
    }
    else{
        writeFIFO->Head++;
    }

    //increment size and release mutex
    G8RTOS_SignalSemaphore(&writeFIFO->CurrentSize);
    G8RTOS_SignalSemaphore(&writeFIFO->Mutex);

    return NO_ERROR;
}

