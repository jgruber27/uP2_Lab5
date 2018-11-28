#include "msp.h"
#include "driverlib.h"
#include "BSP.h"
#include "G8RTOS.h"
#include "G8RTOS_IPC.h"
#include "G8RTOS_Scheduler.h"
#include "LCDLib.h"
#include <time.h>
#include <cc3100_usage.h>
#include "Game.h"

semaphore_t *screen_s;
semaphore_t *wifi_s;
char button_flag;
uint8_t host_score;
uint8_t client_score;

GameState_t game;
GeneralPlayerInfo_t this_player;
SpecificPlayerInfo_t self;
playerType player_type;



void Button_isr(){
    //Delay a little bit
    int i;
    for(i=0;i<0x1fff;i++);

    //check if up-bounce
    if(P4IN & BIT4){
        //reset interrupt flag
        P4IFG &= ~BIT4;
    }
    else {
        //set the touch_flag
        button_flag = 1;
        //reset interrupt flag
        P4IFG &= ~BIT4;
        //disable interrupt
        P4IE &= ~BIT4;
    }
}

/*
 * Thread meant as a startup screen.
 * Waits for touch to start other threads
 */
void Startup(){
    int16_t joyx;
    int16_t joyy;
    char selection;
    char prev_selection;


    G8RTOS_InitSemaphore(&screen_s, 1);

    //Create the startup screen
    G8RTOS_WaitSemaphore(&screen_s);
    LCD_Clear(LCD_BLACK, MAX_SCREEN_X, MAX_SCREEN_Y);
    LCD_DrawRectangle(50, 120, 102, 132, LCD_BLUE);
    LCD_DrawRectangle(190, 260, 102, 132, LCD_RED);
    LCD_DrawRectangle(192, 258, 104, 130, LCD_BLACK);
    LCD_Text(70, 110, "HOST", LCD_RED);
    LCD_Text(202, 110, "CLIENT", LCD_BLUE);
    G8RTOS_SignalSemaphore(&screen_s);

    button_flag = 0;
    selection = 2;
    prev_selection = 2;
    //Enable button interrupt
    P4IE |= BIT4;

    //Wait for a button press
    while(!button_flag){

        //read joystick data
        GetJoystickCoordinates(&joyx, &joyy);
        if(joyx > 2000){
            selection = 2;
        }
        else if(joyx < -2000){
            selection = 1;
        }
        switch(selection){
        case 1:
            if(prev_selection != 1){
                prev_selection = 1;
                G8RTOS_WaitSemaphore(&screen_s);
                LCD_DrawRectangle(52, 118, 104, 130, LCD_BLACK);
                LCD_Text(70, 110, "HOST", LCD_RED);
                LCD_DrawRectangle(190, 260, 102, 132, LCD_RED);
                LCD_Text(202, 110, "CLIENT", LCD_BLUE);
                G8RTOS_SignalSemaphore(&screen_s);
            }
            break;
        case 2:
            if(prev_selection != 2){
                prev_selection = 2;
                G8RTOS_WaitSemaphore(&screen_s);
                LCD_DrawRectangle(192, 258, 104, 130, LCD_BLACK);
                LCD_Text(202, 110, "CLIENT", LCD_BLUE);
                LCD_DrawRectangle(50, 120, 102, 132, LCD_BLUE);
                LCD_Text(70, 110, "HOST", LCD_RED);
                G8RTOS_SignalSemaphore(&screen_s);
            }
            break;
        default:
            break;
        }
        //selection = 0;
    }

    if(selection == 1){
        //client
        player_type = Client;
        this_player.color = PLAYER_BLUE;
        self.playerNumber = TOP;
    } else {
        //host
        player_type = Host;
        this_player.color = PLAYER_RED;
        self.playerNumber = BOTTOM;
    }

    //Time to start, reset the screen
    G8RTOS_WaitSemaphore(&screen_s);
    LCD_DrawRectangle(50, 120, 102, 132, LCD_BLACK);
    LCD_DrawRectangle(190, 260, 102, 132, LCD_BLACK);
    G8RTOS_SignalSemaphore(&screen_s);

    //add functional threads
    if(player_type == Host){
        G8RTOS_AddThread(CreateGame, 1, "Starts Game");
    } else {
        G8RTOS_AddThread(JoinGame, 1, "Starts Game");
    }
    //This thread is no longer useful - kill it
    G8RTOS_KillSelf();
}


/***********************************************************************/
/*                              HOST THREADS                           */
/***********************************************************************/

void CreateGame(){

    host_score = 0;
    client_score = 0;

    game.players[0].currentCenter = PADDLE_X_CENTER;

    LCD_Text(80, 150, "Connecting...", LCD_CYAN);
    initCC3100(Host);

    uint8_t ack = 0;
    uint8_t initdata[4];
    while(ack != 37){
        ReceiveData(&ack, 1);
    }

    ReceiveData(&initdata, 4);

    uint32_t client_ip = initdata[0] << 24 | initdata[1] << 16 | initdata[2] << 8 | initdata[3];
    ack = 44;
    SendData(&ack, client_ip, 1);

    LCD_Text(190, 150, "Done!", LCD_CYAN);

    //create the initial board
    InitBoardState();

    G8RTOS_AddThread(ReadJoystickHost, 2, "Reads Joystick");
    G8RTOS_AddThread(DrawObjects, 2, "Updates Objects");
    G8RTOS_AddThread(ReceiveDataFromClient, 2, "send data");

    //kill self
    G8RTOS_KillSelf();
}


/*
 * Thread that sends game state to client
 */
void SendDataToClient(){

}


/*
 * Thread that receives UDP packets from client
 */
void ReceiveDataFromClient(){

    uint8_t data[9];
    uint16_t x_start, x_end, y_start, y_end;

    while(1){
        G8RTOS_WaitSemaphore(wifi_s);
        ReceiveData(data,9);
        G8RTOS_SignalSemaphore(wifi_s);

        if(data[0] != 0){
            asm("   nop");
            sleep(50);
            continue;
        }

        x_start = data[1] << 8 | data[2];
        x_end = data[3] << 8 | data[4];
        y_start = data[5] << 8 | data[6];
        y_end = data[7] << 8 | data[8];

        G8RTOS_WaitSemaphore(&screen_s);
        LCD_DrawRectangle(x_start, x_end, y_start, y_end, LCD_MAGENTA);
        G8RTOS_SignalSemaphore(&screen_s);


        sleep(50);
    }


}


/*
 * Generate Ball thread
 */
void GenerateBall();

//Reads the current joystick location
void ReadJoystickHost(){
    int16_t joyx;
    int16_t joyy;
    while(1){
        GetJoystickCoordinates(&joyx, &joyy);
        if(joyx > 2000){
            self.displacement = -5;
        } else if (joyx < -2000){
            self.displacement = 5;
        } else {
            self.displacement = 0;
        }
        sleep(10);
        if(game.players[0].currentCenter + self.displacement > ARENA_MIN_X + PADDLE_LEN_D2 &&
                game.players[0].currentCenter + self.displacement < ARENA_MAX_X - PADDLE_LEN_D2){
            game.players[0].currentCenter += self.displacement;
        }
        sleep(10);
    }

}


/*
 * Thread to move a single ball
 */
void MoveBall();

/*
 * End of game for the host
 */
void EndOfGameHost();

/**********************************************************************/
/*                       End of Host threads                          */
/**********************************************************************/





/***********************************************************************/
/*                              CLIENT THREADS                         */
/***********************************************************************/

/*
 * Thread for client to join game
 */
void JoinGame(){
    LCD_Text(80, 150, "Connecting...", LCD_CYAN);
    initCC3100(Client);

    self.IP_address = getLocalIP();
    self.acknowledge = 0;
    self.displacement = 0;
    self.joined = 0;
    self.playerNumber = TOP;
    self.ready = 0;

    uint8_t start_send[5] = {37, (self.IP_address>>24)&0xff, (self.IP_address>>16)&0xff, (self.IP_address>>8)&0xff, (self.IP_address)&0xff};
    uint8_t ack = 0;
    SendData(start_send, HOST_IP_ADDR, 5);
    while(ack != 44){
        ReceiveData(&ack, 1);

        sleep(50);
    }

    LCD_Text(190, 150, "Done!", LCD_CYAN);

    host_score = 0;
    client_score = 0;

    game.players[1].currentCenter = PADDLE_X_CENTER;
    //create the initial board
    InitBoardState();

    G8RTOS_AddThread(ReadJoystickClient, 2, "Reads Joystick");
    G8RTOS_AddThread(DrawObjects, 2, "Updates Objects");
    G8RTOS_AddThread(SendDataToHost, 1, "receive data");

    //kill self
    G8RTOS_KillSelf();

}

/*
 * Thread that receives game state packets from host
 */
void ReceiveDataFromHost();

/*
 * Thread that sends UDP packets to host
 */
void SendDataToHost(){

    uint8_t data[9] = {0, 0, 100, 0, 120, 0, 100, 0, 120};

    while(1){
        G8RTOS_WaitSemaphore(wifi_s);
        SendData(data, HOST_IP_ADDR, 9);
        G8RTOS_SignalSemaphore(wifi_s);

        sleep(1000);
    }

}

/*
 * Thread to read client's joystick
 */
void ReadJoystickClient(){
    int16_t joyx;
    int16_t joyy;
    while(1){
        GetJoystickCoordinates(&joyx, &joyy);
        if(joyx > 2000){
            self.displacement = -5;
        } else if (joyx < -2000){
            self.displacement = 5;
        } else {
            self.displacement = 0;
        }
        if(game.players[1].currentCenter + self.displacement > ARENA_MIN_X + PADDLE_LEN_D2 &&
                game.players[1].currentCenter + self.displacement < ARENA_MAX_X - PADDLE_LEN_D2){
            game.players[1].currentCenter += self.displacement;
        }
        sleep(20);
    }

}

/*
 * End of game for the client
 */
void EndOfGameClient();

/**********************************************************************/
/*                       End of Client threads                        */
/**********************************************************************/




/**********************************************************************/
/*                       Common Threads                               */
/**********************************************************************/

//empty thread so we don't break the OS
void IdleThread(){
    while(1);
}

/*
 * Thread to draw all the objects in the game
 */
void DrawObjects(){
    PrevPlayer_t prev_player_loc;
    if(player_type == Host){
        prev_player_loc.Center = game.players[0].currentCenter;
    } else {
        prev_player_loc.Center = game.players[1].currentCenter;
    }


    while(1){
        if(player_type == Host){
            UpdatePlayerOnScreen(&prev_player_loc, &(game.players[0]));
        } else {
            UpdatePlayerOnScreen(&prev_player_loc, &(game.players[1]));
        }
        sleep(20);
    }
}


/*
 * Thread to update LEDs based on score
 */
void MoveLEDs();




/**********************************************************************/
/*                       End of Common threads                        */
/**********************************************************************/


/**********************************************************************/
/*                       Public Functions                             */
/**********************************************************************/
void InitBoardState(){
    char str[10];
    G8RTOS_WaitSemaphore(&screen_s);
    //Draw the bounds
    LCD_DrawRectangle(ARENA_MIN_X, ARENA_MAX_X, ARENA_MIN_Y, ARENA_MAX_Y, LCD_WHITE);
    LCD_DrawRectangle(ARENA_MIN_X+1, ARENA_MAX_X-1, ARENA_MIN_Y, ARENA_MAX_Y, BACK_COLOR);

    DrawPlayer(&(game.players[0]));
    DrawPlayer(&(game.players[1]));


    snprintf(str, 10, "%d", host_score);
    LCD_Text(0, 0, str, LCD_BLUE);
    snprintf(str, 10, "%d", client_score);
    LCD_Text(0, 225, str, LCD_RED);
    G8RTOS_SignalSemaphore(&screen_s);

}


void DrawPlayer(GeneralPlayerInfo_t * player){
    //Draw the Paddles

    if(player == &(game.players[0])){
    LCD_DrawRectangle((PADDLE_X_CENTER)-PADDLE_LEN_D2, (PADDLE_X_CENTER)+PADDLE_LEN_D2,
                      TOP_PLAYER_CENTER_Y-PADDLE_WID_D2, TOP_PLAYER_CENTER_Y+PADDLE_WID_D2, PLAYER_BLUE);
    } else {
    LCD_DrawRectangle((PADDLE_X_CENTER)-PADDLE_LEN_D2, (PADDLE_X_CENTER)+PADDLE_LEN_D2,
                      BOTTOM_PLAYER_CENTER_Y-PADDLE_WID_D2, BOTTOM_PLAYER_CENTER_Y+PADDLE_WID_D2, PLAYER_RED);
    }

}


void UpdatePlayerOnScreen(PrevPlayer_t * prevPlayerIn, GeneralPlayerInfo_t * outPlayer){
    int16_t distance =  outPlayer->currentCenter - prevPlayerIn->Center;

    if(distance == 0){
        return;
    }

    G8RTOS_WaitSemaphore(&screen_s);

    if(outPlayer == &(game.players[0])){
        if(distance > 0){
            LCD_DrawRectangle(prevPlayerIn->Center + PADDLE_LEN_D2, prevPlayerIn->Center + PADDLE_LEN_D2 + distance,
                              BOTTOM_PLAYER_CENTER_Y-PADDLE_WID_D2, BOTTOM_PLAYER_CENTER_Y+PADDLE_WID_D2, PLAYER_RED);
            LCD_DrawRectangle(prevPlayerIn->Center - PADDLE_LEN_D2, prevPlayerIn->Center - PADDLE_LEN_D2 + distance,
                              BOTTOM_PLAYER_CENTER_Y-PADDLE_WID_D2, BOTTOM_PLAYER_CENTER_Y+PADDLE_WID_D2, BACK_COLOR);
        } else {
            LCD_DrawRectangle(prevPlayerIn->Center - PADDLE_LEN_D2 + distance, prevPlayerIn->Center - PADDLE_LEN_D2,
                              BOTTOM_PLAYER_CENTER_Y-PADDLE_WID_D2, BOTTOM_PLAYER_CENTER_Y+PADDLE_WID_D2, PLAYER_RED);
            LCD_DrawRectangle(prevPlayerIn->Center + PADDLE_LEN_D2 + distance, prevPlayerIn->Center + PADDLE_LEN_D2,
                              BOTTOM_PLAYER_CENTER_Y-PADDLE_WID_D2, BOTTOM_PLAYER_CENTER_Y+PADDLE_WID_D2, BACK_COLOR);
        }
    } else {
        if(distance > 0){
            LCD_DrawRectangle(prevPlayerIn->Center + PADDLE_LEN_D2, prevPlayerIn->Center + PADDLE_LEN_D2 + distance,
                              TOP_PLAYER_CENTER_Y-PADDLE_WID_D2, TOP_PLAYER_CENTER_Y+PADDLE_WID_D2, PLAYER_BLUE);
            LCD_DrawRectangle(prevPlayerIn->Center - PADDLE_LEN_D2, prevPlayerIn->Center - PADDLE_LEN_D2 + distance,
                              TOP_PLAYER_CENTER_Y-PADDLE_WID_D2, TOP_PLAYER_CENTER_Y+PADDLE_WID_D2, BACK_COLOR);
        } else {
            LCD_DrawRectangle(prevPlayerIn->Center - PADDLE_LEN_D2 + distance, prevPlayerIn->Center - PADDLE_LEN_D2,
                              TOP_PLAYER_CENTER_Y-PADDLE_WID_D2, TOP_PLAYER_CENTER_Y+PADDLE_WID_D2, PLAYER_BLUE);
            LCD_DrawRectangle(prevPlayerIn->Center + PADDLE_LEN_D2 + distance, prevPlayerIn->Center + PADDLE_LEN_D2,
                              TOP_PLAYER_CENTER_Y-PADDLE_WID_D2, TOP_PLAYER_CENTER_Y+PADDLE_WID_D2, BACK_COLOR);
        }
    }

    G8RTOS_SignalSemaphore(&screen_s);

    prevPlayerIn->Center = outPlayer->currentCenter;
}

/*
 * Function updates ball position on screen
 */
void UpdateBallOnScreen(PrevBall_t * previousBall, Ball_t * currentBall, uint16_t outColor){

}


/**********************************************************************/
/*                       End of Public Functions                      */
/**********************************************************************/





