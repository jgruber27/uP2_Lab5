#include <Game.h>
#include "G8RTOS.h"
//#include "Threads.h"
#include <time.h>


void main(void){

    //init Button 0 to be an input interrupt
            //triggering on a falling edge
            P4DIR &= ~BIT4;
            P4IFG &= ~BIT4;
            //P4IE |= BIT4;
            P4IES |= BIT4;
            P4REN |= BIT4;
            P4OUT |= BIT4;

    //create a random seed
    srand(time(NULL));

    //Initialize the OS, board, and LCD
    G8RTOS_Init();

    //Add an idle thread as thread 1, with low priority
    G8RTOS_AddThread(IdleThread,5, "idle");

    //Add a startup thread, which creates the other threads
    G8RTOS_AddThread(Startup, 0, "Start");

    //Add the ISR for the touch screen
    G8RTOS_AddAPeriodicEvent(Button_isr, 0, PORT4_IRQn);

    //Start the OS
    G8RTOS_Launch();
}
