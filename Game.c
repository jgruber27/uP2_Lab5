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
PrevBall_t previous_loc[MAX_NUM_OF_BALLS];
uint8_t CurrentNumberOfBalls;
uint32_t threadId_table[10];



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

    P2DIR |= (BLUE_LED | RED_LED);
    P2OUT &= ~(BLUE_LED | RED_LED);

    G8RTOS_InitSemaphore(screen_s, 1);
    G8RTOS_InitSemaphore(wifi_s, 1);

    //Create the startup screen
    G8RTOS_WaitSemaphore(screen_s);
    LCD_Clear(LCD_BLACK, MAX_SCREEN_X, MAX_SCREEN_Y);
    LCD_DrawRectangle(50, 120, 102, 132, LCD_BLUE);
    LCD_DrawRectangle(190, 260, 102, 132, LCD_RED);
    LCD_DrawRectangle(192, 258, 104, 130, LCD_BLACK);
    LCD_Text(70, 110, "HOST", LCD_RED);
    LCD_Text(202, 110, "CLIENT", LCD_BLUE);
    G8RTOS_SignalSemaphore(screen_s);

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
                G8RTOS_WaitSemaphore(screen_s);
                LCD_DrawRectangle(52, 118, 104, 130, LCD_BLACK);
                LCD_Text(70, 110, "HOST", LCD_RED);
                LCD_DrawRectangle(190, 260, 102, 132, LCD_RED);
                LCD_Text(202, 110, "CLIENT", LCD_BLUE);
                G8RTOS_SignalSemaphore(screen_s);
            }
            break;
        case 2:
            if(prev_selection != 2){
                prev_selection = 2;
                G8RTOS_WaitSemaphore(screen_s);
                LCD_DrawRectangle(192, 258, 104, 130, LCD_BLACK);
                LCD_Text(202, 110, "CLIENT", LCD_BLUE);
                LCD_DrawRectangle(50, 120, 102, 132, LCD_BLUE);
                LCD_Text(70, 110, "HOST", LCD_RED);
                G8RTOS_SignalSemaphore(screen_s);
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
    G8RTOS_WaitSemaphore(screen_s);
    LCD_DrawRectangle(50, 120, 102, 132, LCD_BLACK);
    LCD_DrawRectangle(190, 260, 102, 132, LCD_BLACK);
    G8RTOS_SignalSemaphore(screen_s);

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

    LCD_Text(80, 150, "Connecting...", LCD_CYAN);
    initCC3100(Host);

    uint8_t ack = 0;
    uint8_t initdata[4];
    while(ack != 37){
        ReceiveData(&ack, 1);
    }

    ReceiveData(&initdata, 4);

    uint32_t client_ip = initdata[0] << 24 | initdata[1] << 16 | initdata[2] << 8 | initdata[3];

    self.IP_address = client_ip;

    ack = 44;
    SendData(&ack, client_ip, 1);

    while(ack != 0xFF){
        ReceiveData(&ack, 1);
    }

    game.players[0].color = PLAYER_RED;
    game.players[0].currentCenter = PADDLE_X_CENTER;
    game.players[0].position = BOTTOM;

    game.players[1].color = PLAYER_BLUE;
    game.players[1].currentCenter = PADDLE_X_CENTER;
    game.players[1].position = TOP;

    game.LEDScores[0] = 0;
    game.LEDScores[1] = 0;

    LCD_Text(190, 150, "Done!", LCD_CYAN);
    P2OUT |= RED_LED;

    //create the initial board
    InitBoardState();

    G8RTOS_AddThread(ReadJoystickHost, 2, "Reads Joystick");
    G8RTOS_AddThread(DrawObjects, 2, "Updates Objects");
    G8RTOS_AddThread(ReceiveDataFromClient, 1, "get data");
    G8RTOS_AddThread(SendDataToClient, 2, "send data");
    G8RTOS_AddThread(GenerateBall, 1, "makes balls");
    G8RTOS_AddThread(MoveLEDs, 2, "Update leds");
    G8RTOS_AddThread(EndOfGameHost, 0, "end of game handler");

    //kill self
    G8RTOS_KillSelf();
}


/*
 * Thread that sends game state to client
 */
void SendDataToClient(){
    threadId_table[0] = G8RTOS_GetThreadId();
    while(1){
        G8RTOS_WaitSemaphore(wifi_s);
        SendData(&game, self.IP_address, sizeof(game));
        G8RTOS_SignalSemaphore(wifi_s);
        sleep(20);
    }
}


/*
 * Thread that receives UDP packets from client
 */
void ReceiveDataFromClient(){
    threadId_table[1] = G8RTOS_GetThreadId();

    while(1){

        G8RTOS_WaitSemaphore(wifi_s);
        ReceiveData(&self,sizeof(self));
        G8RTOS_SignalSemaphore(wifi_s);


        if(game.players[1].currentCenter + self.displacement > ARENA_MIN_X + PADDLE_LEN_D2 &&
                        game.players[1].currentCenter + self.displacement < ARENA_MAX_X - PADDLE_LEN_D2){
                    game.players[1].currentCenter += self.displacement;
                }


        sleep(20);
    }


}


/*
 * Generate Ball thread
 */
void GenerateBall(){
    threadId_table[2] = G8RTOS_GetThreadId();

    CurrentNumberOfBalls = 0;


    while(1){
        G8RTOS_AddThread(MoveBall, 2, "Moves balls");
        CurrentNumberOfBalls++;
        sleep(500*CurrentNumberOfBalls);
    }
}

//Reads the current joystick location
void ReadJoystickHost(){
    threadId_table[3] = G8RTOS_GetThreadId();
    int16_t joyx;
    int16_t joyy;
    while(1){
        GetJoystickCoordinates(&joyx, &joyy);
        if(joyx > 2000){
            game.player.displacement = -5;
        } else if (joyx < -2000){
            game.player.displacement = 5;
        } else {
            game.player.displacement = 0;
        }
        sleep(10);
        if(game.players[0].currentCenter + game.player.displacement > ARENA_MIN_X + PADDLE_LEN_D2 &&
                game.players[0].currentCenter + game.player.displacement < ARENA_MAX_X - PADDLE_LEN_D2){
            game.players[0].currentCenter += game.player.displacement;
        }
        sleep(10);
    }

}


/*
 * Thread to move a single ball
 */
void MoveBall(){

    //Find a dead ball
    int num = 0;
    while(game.balls[num].alive == 1){
        num++;

        //No dead balls... terminate thread
        if(num == MAX_NUM_OF_BALLS){
            G8RTOS_KillSelf();
        }
    }

    game.balls[num].alive = 1;
    game.balls[num].color = LCD_WHITE;
    game.balls[num].currentCenterX = ARENA_MAX_X/2 + ARENA_MIN_X/2;
    game.balls[num].currentCenterY = ARENA_MAX_Y/2 + ARENA_MIN_Y/2;
    game.balls[num].velocityX = 2;
    game.balls[num].velocityY = 3;
    game.balls[num].threadId = G8RTOS_GetThreadId();
    CurrentNumberOfBalls++;

    int32_t w, h, dx, dy, wy, hx;
    int8_t check_hit;

    while(1){
        check_hit = 0;
        previous_loc[num].CenterX = game.balls[num].currentCenterX;
        previous_loc[num].CenterY = game.balls[num].currentCenterY;
        game.balls[num].currentCenterX += game.balls[num].velocityX;
        game.balls[num].currentCenterY += game.balls[num].velocityY;

        //did we hit a wall
        if(game.balls[num].currentCenterX >= HORIZ_CENTER_MAX_BALL){
            game.balls[num].velocityX *= -1;
            game.balls[num].currentCenterX = HORIZ_CENTER_MAX_BALL-2;
        } else if (game.balls[num].currentCenterX <= HORIZ_CENTER_MIN_BALL){
            game.balls[num].velocityX *= -1;
            game.balls[num].currentCenterX = HORIZ_CENTER_MIN_BALL+2;
        }

        //did we hit a paddle
        for(int i = 0; i < 2; i++){
            w = 0.5 * (BALL_SIZE+1 + PADDLE_LEN+1);
            h = 0.5 * (BALL_SIZE+1 + PADDLE_WID+1);
            dx = game.balls[num].currentCenterX - game.players[i].currentCenter;
            if(i == 0){
                dy = game.balls[num].currentCenterY - BOTTOM_PLAYER_CENTER_Y;
            } else {
                dy = game.balls[num].currentCenterY - TOP_PLAYER_CENTER_Y;
            }
            if (abs(dx) <= w && abs(dy) <= h){/* collision! */
                check_hit = 1;
                wy = w * dy;
                hx = h * dx;
                if (wy > hx){
                   /* collision at the top of A */
                    game.balls[num].velocityY *= -1;
                    game.balls[num].color = LCD_BLUE;
                    //check which side of paddle was hit
                    if(dx < -10){
                        game.balls[num].velocityX = -2;
                    } else if(dx > 10){
                        game.balls[num].velocityX = 2;
                    } else {
                        game.balls[num].velocityX = 0;
                    }

                }else{
                    /* the bottom of A was hit */
                    game.balls[num].velocityY *= -1;
                    game.balls[num].color = LCD_RED;
                    //check which side of paddle was hit
                    if(dx < -10){
                        game.balls[num].velocityX = -2;
                    } else if(dx > 10){
                        game.balls[num].velocityX = 2;
                    } else {
                        game.balls[num].velocityX = 0;
                    }
                }
                break;
            }
        }

        //did we pass a paddle
        if(!check_hit){
            if(game.balls[num].currentCenterY > VERT_CENTER_MAX_BALL){
                game.balls[num].velocityY *= -1;
                if(game.balls[num].color != LCD_WHITE){
                    game.LEDScores[1]++;
                }
                KillBall(&(game.balls[num]));
                CurrentNumberOfBalls--;

            } else if (game.balls[num].currentCenterY < VERT_CENTER_MIN_BALL){
                game.balls[num].velocityY *= -1;
                if(game.balls[num].color != LCD_WHITE){
                    game.LEDScores[0]++;
                }
                KillBall(&(game.balls[num]));
                CurrentNumberOfBalls--;
            }

            if(game.LEDScores[0] == 8){
                game.gameDone = 1;
                game.winner = 0;
            } else if(game.LEDScores[1] == 8){
                game.gameDone = 1;
                game.winner = 1;
            }
        }
        sleep(35);
    }
}

/*
 * End of game for the host
 */
void EndOfGameHost(){
    while(1){
        if(game.gameDone){
            if(game.winner){
                G8RTOS_WaitSemaphore(screen_s);
                LCD_DrawRectangle(0, MAX_SCREEN_X, 0, MAX_SCREEN_Y, LCD_BLUE);
                LCD_Text(130, 110, "BLUE WINS", LCD_BLACK);
                G8RTOS_SignalSemaphore(screen_s);
                LP3943_LedModeSet(RED, 0);
                G8RTOS_KillThread(threadId_table[0]);
                G8RTOS_KillThread(threadId_table[1]);
                G8RTOS_KillThread(threadId_table[2]);
                G8RTOS_KillThread(threadId_table[3]);

                G8RTOS_KillThread(threadId_table[5]);
                G8RTOS_KillThread(threadId_table[6]);

                for(int i = 0; i < MAX_NUM_OF_BALLS; i++){
                    G8RTOS_KillThread(game.balls[i].threadId);
                }

                while(1){
                    LP3943_LedModeSet(BLUE, 0xffff);
                    sleep(250);
                    LP3943_LedModeSet(BLUE, 0x0000);
                    sleep(250);
                }
            }
            else {
                G8RTOS_WaitSemaphore(screen_s);
                LCD_DrawRectangle(0, MAX_SCREEN_X, 0, MAX_SCREEN_Y, LCD_RED);
                LCD_Text(130, 110, "RED WINS", LCD_BLACK);
                G8RTOS_SignalSemaphore(screen_s);
                LP3943_LedModeSet(BLUE, 0);
                G8RTOS_KillThread(threadId_table[0]);
                G8RTOS_KillThread(threadId_table[1]);
                G8RTOS_KillThread(threadId_table[2]);
                G8RTOS_KillThread(threadId_table[3]);

                G8RTOS_KillThread(threadId_table[5]);
                G8RTOS_KillThread(threadId_table[6]);

                for(int i = 0; i < MAX_NUM_OF_BALLS; i++){
                    G8RTOS_KillThread(game.balls[i].threadId);
                }

                while(1){
                    LP3943_LedModeSet(RED, 0xffff);
                    sleep(250);
                    LP3943_LedModeSet(RED, 0x0000);
                    sleep(250);
                }
            }
        } else {
            sleep(500);
        }
    }
}

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


    while(ack != 44){
        ReceiveData(&ack, 1);
        SendData(start_send, HOST_IP_ADDR, 5);
        sleep(50);
    }

    ack = 0xFF;

    SendData(&ack, HOST_IP_ADDR, 1);

    sleep(10);

    LCD_Text(190, 150, "Done!", LCD_CYAN);
    P2OUT |= BLUE_LED;

    host_score = 0;
    client_score = 0;

    game.players[0].color = PLAYER_RED;
    game.players[0].currentCenter = PADDLE_X_CENTER;
    game.players[0].position = BOTTOM;

    game.players[1].color = PLAYER_BLUE;
    game.players[1].currentCenter = PADDLE_X_CENTER;
    game.players[1].position = TOP;
    //create the initial board
    InitBoardState();

    G8RTOS_AddThread(ReadJoystickClient, 2, "Reads Joystick");
    G8RTOS_AddThread(DrawObjects, 2, "Updates Objects");
    G8RTOS_AddThread(SendDataToHost, 2, "send data");
    G8RTOS_AddThread(ReceiveDataFromHost, 1, "receive data");
    G8RTOS_AddThread(MoveLEDs, 2, "Update leds");
    G8RTOS_AddThread(EndOfGameClient, 0, "end of game handler");

    //kill self
    G8RTOS_KillSelf();

}

/*
 * Thread that receives game state packets from host
 */
void ReceiveDataFromHost(){
    threadId_table[0] = G8RTOS_GetThreadId();

    while(1){
        for(int i = 0; i < MAX_NUM_OF_BALLS; i++){
            previous_loc[i].CenterX = game.balls[i].currentCenterX;
            previous_loc[i].CenterY = game.balls[i].currentCenterY;
        }
        G8RTOS_WaitSemaphore(wifi_s);
        ReceiveData(&game, sizeof(game));
        G8RTOS_SignalSemaphore(wifi_s);


        sleep(20);
    }

}

/*
 * Thread that sends UDP packets to host
 */
void SendDataToHost(){
    threadId_table[1] = G8RTOS_GetThreadId();

    while(1){
        G8RTOS_WaitSemaphore(wifi_s);
        SendData(&self, HOST_IP_ADDR, sizeof(self));
        G8RTOS_SignalSemaphore(wifi_s);
        sleep(20);
    }

}

/*
 * Thread to read client's joystick
 */
void ReadJoystickClient(){
    threadId_table[2] = G8RTOS_GetThreadId();
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
void EndOfGameClient(){
    while(1){
        if(game.gameDone){
            //sleep(100);
            if(game.winner){
                G8RTOS_WaitSemaphore(screen_s);
                LCD_DrawRectangle(0, MAX_SCREEN_X, 0, MAX_SCREEN_Y, LCD_BLUE);
                LCD_Text(130, 110, "BLUE WINS", LCD_BLACK);
                G8RTOS_SignalSemaphore(screen_s);
                LP3943_LedModeSet(RED, 0);

                G8RTOS_KillThread(threadId_table[0]);
                G8RTOS_KillThread(threadId_table[1]);
                G8RTOS_KillThread(threadId_table[2]);
                G8RTOS_KillThread(threadId_table[5]);
                G8RTOS_KillThread(threadId_table[6]);
                while(1){
                    LP3943_LedModeSet(BLUE, 0xffff);
                    sleep(250);
                    LP3943_LedModeSet(BLUE, 0x0000);
                    sleep(250);
                }
            } else {
                G8RTOS_WaitSemaphore(screen_s);
                LCD_DrawRectangle(0, MAX_SCREEN_X, 0, MAX_SCREEN_Y, LCD_RED);
                LCD_Text(130, 110, "RED WINS", LCD_BLACK);
                G8RTOS_SignalSemaphore(screen_s);
                LP3943_LedModeSet(BLUE, 0);

                G8RTOS_KillThread(threadId_table[0]);
                G8RTOS_KillThread(threadId_table[1]);
                G8RTOS_KillThread(threadId_table[2]);
                G8RTOS_KillThread(threadId_table[5]);
                G8RTOS_KillThread(threadId_table[6]);
                while(1){
                    LP3943_LedModeSet(RED, 0xffff);
                    sleep(250);
                    LP3943_LedModeSet(RED, 0x0000);
                    sleep(250);
                }
            }
        } else {
            sleep(500);
        }
    }
}

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
    threadId_table[5] = G8RTOS_GetThreadId();
    PrevPlayer_t prev_player_loc, prev_player_loc2;

    prev_player_loc.Center = game.players[0].currentCenter;
    prev_player_loc2.Center = game.players[1].currentCenter;



    while(1){
        UpdatePlayerOnScreen(&prev_player_loc, &(game.players[0]));
        UpdatePlayerOnScreen(&prev_player_loc2, &(game.players[1]));
        for(int i = 0; i < MAX_NUM_OF_BALLS; i++){
            if(game.balls[i].alive){
                UpdateBallOnScreen(&previous_loc[i], &(game.balls[i]), LCD_BLACK);
            }
        }
        sleep(20);
    }
}


/*
 * Thread to update LEDs based on score
 */
void MoveLEDs(){
    threadId_table[6] = G8RTOS_GetThreadId();
    int16_t leds;
    int16_t led_options[9] = {0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff};
    while(1){
        leds = 0xff0000 >> game.LEDScores[0];
        LP3943_LedModeSet(RED, leds);
        leds = led_options[game.LEDScores[1]];
        LP3943_LedModeSet(BLUE, leds);
        sleep(200);
    }
}




/**********************************************************************/
/*                       End of Common threads                        */
/**********************************************************************/


/**********************************************************************/
/*                       Public Functions                             */
/**********************************************************************/
void InitBoardState(){
    char str[10];
    G8RTOS_WaitSemaphore(screen_s);
    //Draw the bounds
    LCD_DrawRectangle(ARENA_MIN_X, ARENA_MAX_X, ARENA_MIN_Y, ARENA_MAX_Y, LCD_WHITE);
    LCD_DrawRectangle(ARENA_MIN_X+1, ARENA_MAX_X-1, ARENA_MIN_Y, ARENA_MAX_Y, BACK_COLOR);

    DrawPlayer(&(game.players[0]));
    DrawPlayer(&(game.players[1]));


    snprintf(str, 10, "%d", host_score);
    LCD_Text(0, 0, str, LCD_BLUE);
    snprintf(str, 10, "%d", client_score);
    LCD_Text(0, 225, str, LCD_RED);
    G8RTOS_SignalSemaphore(screen_s);

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

    G8RTOS_WaitSemaphore(screen_s);

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

    G8RTOS_SignalSemaphore(screen_s);

    prevPlayerIn->Center = outPlayer->currentCenter;
}

/*
 * Function updates ball position on screen
 */
void UpdateBallOnScreen(PrevBall_t * previousBall, Ball_t * currentBall, uint16_t outColor){
    //check if the ball actually moved
    if(previousBall->CenterX == currentBall->currentCenterX && previousBall->CenterY == currentBall->currentCenterY){
        return;
    }

    G8RTOS_WaitSemaphore(screen_s);
    LCD_DrawRectangle(previousBall->CenterX-BALL_SIZE_D2, previousBall->CenterX+BALL_SIZE_D2,
                      previousBall->CenterY-BALL_SIZE_D2, previousBall->CenterY+BALL_SIZE_D2, LCD_BLACK);
    LCD_DrawRectangle(currentBall->currentCenterX-BALL_SIZE_D2, currentBall->currentCenterX+BALL_SIZE_D2,
                      currentBall->currentCenterY-BALL_SIZE_D2, currentBall->currentCenterY+BALL_SIZE_D2, currentBall->color);
    G8RTOS_SignalSemaphore(screen_s);

}


void KillBall(Ball_t * currentBall){
    currentBall->alive = 0;
    G8RTOS_WaitSemaphore(screen_s);
    LCD_DrawRectangle(currentBall->currentCenterX-BALL_SIZE_D2, currentBall->currentCenterX+BALL_SIZE_D2,
                      currentBall->currentCenterY-BALL_SIZE_D2, currentBall->currentCenterY+BALL_SIZE_D2, LCD_BLACK);
    G8RTOS_SignalSemaphore(screen_s);
    G8RTOS_KillThread(currentBall->threadId);
}

/**********************************************************************/
/*                       End of Public Functions                      */
/**********************************************************************/
