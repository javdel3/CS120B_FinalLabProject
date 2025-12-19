/*        Javier Delgado
          jdelg108@ucr.edu

*          Discussion Section: 24

 *         Assignment: Final Project

 *         Exercise Description: Brick Breaker

 *         I acknowledge all content contained herein, excluding template or example code, is my own original work.

 *         Demo Link: https://www.youtube.com/watch?v=RnSAQB_eWJc

 */
#include "timerISR.h"
#include "helper.h"
#include "periph.h"
#include "spiAVR.h"
#include "LCD.h"
#include <stdio.h>
#include "byteArrays.h"
#include "EEPROM.h"

#define NUM_TASKS 7 //Number of tasks being used

//Global vars for cross task comms
bool HALT_GAME = 1; //Stop all necessary synch SMs
bool resetGame = 0;
bool gameWon = 0;
bool gameLost = 0;
bool START_GAME = 1;
unsigned int joystick_global_x_axis;
unsigned int joystick_global_y_axis;
int playerScore;
char scoreBuffer[10]; //Hold up to 10 characters to be printed to LCD
char Display16x2_Score[7] = {'S','c','o','r','e',':','\0'}; //Null terminated string 
char Display16x2_Start[15] = {
    'P','r','e','s','s',' ',
    't','o',' ',
    'B','e','g','i','n','\0'
};
char Display16x2_Won[10] = {
    'Y','o','u',' ',
    'W','i','n','!','!','\0'
};
char Display16x2_Lost[9] = {
    'Y','o','u',' ',
    'L','o','s','t','\0'
};
char paddle_xpos = 52; //Start at center
char left_PaddleRegion1 = 52; 
char left_PaddleRegion2 = 64;
char middle_PaddleRegion1 = 65;
char middle_PaddleRegion2 = 77;
char right_PaddleRegion1 = 78;
char right_PaddleRegion2 = 90;
unsigned char ball_xpos = 60; //Start above paddle
unsigned char ball_ypos = 100;
int ball_xspeed = 0; //Controls how many pixels the ball shifts along x axis
int ball_yspeed = 5; //Controls how many pixels the ball shifts along y axis
int ball_xdirection_flag = -1;
int ball_ydirection_flag = 1; 
bool hit_RightPaddle = 0; //Change direction of ball if hit paddle at certain angle
bool hit_MiddlePaddle = 1;
bool hit_LeftPaddle = 0;
//Variables to help with ball collision
    bool xcoordsMatch;
    bool ycoordsMatch;
    bool brickDoesExist = 1;
    int savedYCoordIndex;
    int savedXCoordIndex;
int bricksDestroyedCounter = 0; //Every 5 bricks destroyed increase speed by 1
int prevBricksDestroyed = 0;

//Bricks start w/ Brick 1 at top left and Brick 25 at bottom right
bool bricksExist[25] = {
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1
};

//{row1, row2, col1, col2}
char brickBoundary[25][4] = {
    //Brick Row 1
    {15,24, 13,32},
    {15,24, 34,53},
    {15,24, 55,74},
    {15,24, 76,95},
    {15,24, 97,116},

    //Brick Row 2
    {26,35, 13,32},
    {26,35, 34,53},
    {26,35, 55,74},
    {26,35, 76,95},
    {26,35, 97,116},

    //Brick Row 3
    {37,46, 13,32},
    {37,46, 34,53},
    {37,46, 55,74},
    {37,46, 76,95},
    {37,46, 97,116},

    //Brick Row 4
    {48,57, 13,32},
    {48,57, 34,53},
    {48,57, 55,74},
    {48,57, 76,95},
    {48,57, 97,116},

    //Brick Row 4
    {59,68, 13,32},
    {59,68, 34,53},
    {59,68, 55,74},
    {59,68, 76,95},
    {59,68, 97,116}
};

//Task struct for concurrent synchSMs implmentations
typedef struct _task{
	signed 	 char state; 		//Task's current state
	unsigned long period; 		//Task period
	unsigned long elapsedTime; 	//Time elapsed since last task tick
	int (*TickFct)(int); 		//Task tick function
} task;

//EEPROM data struct to handle game data
typedef struct {
    bool saved_HALT_GAME;
    bool saved_gameWon;
    bool saved_gameLost;
    bool saved_START_GAME;
    int saved_ball_yspeed;
    int saved_bricksDestroyedCounter;
    int saved_prevBricksDestroyed;
    unsigned long int saved_playerScore;
    bool saved_bricksExist[25]; //EEPROM cannot store pointers so just make a copy
} EEPROM_DATA;

//Place savedGameData var name w/ new EEPROM_DATA var type in EEMEM instead of SRAM
EEPROM_DATA EEMEM savedGameData;

//Define Periods for each task
const unsigned long TASK1_PERIOD = 20;   //Read Joystick
const unsigned long TASK2_PERIOD = 5;    //Physics of ball when contacts paddle
const unsigned long TASK3_PERIOD = 20;   //Update ST7735 128x128 LCD & Ball's Active Movement 
const unsigned long TASK4_PERIOD = 10;   //Update Lab Kit 16x2
const unsigned long TASK5_PERIOD = 10;   //Tracks brick-ball collision & updates bricks displayed 
const unsigned long TASK6_PERIOD = 5;    //Controls what game state player is in (Starting, PLaying, Won, Lost) & Keeps tracks of player score
const unsigned long TASK7_PERIOD = 10;   //Reset Game
const unsigned long GCD_PERIOD = 5;

task tasks[NUM_TASKS]; //Declared task array with number of tasks

void TimerISR() {
	for ( unsigned int i = 0; i < NUM_TASKS; i++ ) {                // Iterate through each task in the task array
		if ( tasks[i].elapsedTime == tasks[i].period ) {           // Check if the task is ready to tick
			tasks[i].state = tasks[i].TickFct(tasks[i].state); // Tick and set the next state for this task
			tasks[i].elapsedTime = 0;                          // Reset the elapsed time for the next tick
		}
		tasks[i].elapsedTime += GCD_PERIOD;                        // Increment the elapsed time by GCD_PERIOD
	}
}

//Helper Functions for components
void clearST7735Window(char col1, char col2, char row1, char row2, long window_area) {
    //Set desired window on LCD to black
    COL_SET(col1,col2);
    ROW_SET(row1,row2);
    MEM_WR(0x00,0x00,0x00,window_area);
}

void setWhiteBackground() {
    //Set entire background on LCD to white
    COL_SET(0,129);
    ROW_SET(0,128);
    MEM_WR(0xFF,0xFF,0xFF,16770);
}

void saveGame() {
    EEPROM_DATA temp;

    //Write from temp (RAM) to savedGameData (EEPROM memory) for the length of EEPROM_DATA
    for (int i = 0; i < 25; i++) {
        temp.saved_bricksExist[i] = bricksExist[i];
    }

    temp.saved_HALT_GAME = HALT_GAME;
    temp.saved_gameWon = gameWon;
    temp.saved_gameLost = gameLost;
    temp.saved_START_GAME = START_GAME;
    temp.saved_playerScore = playerScore;
    temp.saved_ball_yspeed = ball_yspeed;
    temp.saved_bricksDestroyedCounter = bricksDestroyedCounter;
    temp.saved_prevBricksDestroyed = prevBricksDestroyed;

    eeprom_update_block(&temp, &savedGameData, sizeof(EEPROM_DATA));
}

void loadGame() {
    EEPROM_DATA temp;

    //Read from savedGameData (EEPROM memory) and writes into temp (RAM) for the length of EEPROM_DATA
    eeprom_read_block(&temp, &savedGameData, sizeof(EEPROM_DATA));

    HALT_GAME = temp.saved_HALT_GAME;
    gameWon = temp.saved_gameWon;
    gameLost = temp.saved_gameLost;
    START_GAME = temp.saved_START_GAME;
    playerScore = temp.saved_playerScore;
    ball_yspeed = temp.saved_ball_yspeed;
    bricksDestroyedCounter = temp.saved_bricksDestroyedCounter;
    prevBricksDestroyed = temp.saved_prevBricksDestroyed;

    for (int i = 0; i < 25; i++) {
        bricksExist[i] = temp.saved_bricksExist[i];
    }

    //Control what background to display on ST7735 based on game state
    if(HALT_GAME) {
        setWhiteBackground();
    }
    else {
        //Set background black
        clearST7735Window(0,129,0,128,16770); 
        //Display Initial Bricks
        Bricks_INIT();
    }
}

//Note: Update_Block writes continous sequence of bytes vs Update_Byte writes single byte to eeprom

enum readJoystick_States {readJoystick_start, readJoystick_READ};
enum paddleChangeBallDirection_States {paddleChangeBallDirection_start, paddleChangeBallDirection_HOLD, paddleChangeBallDirection_UPDATE};
enum st7735LCD_States {st7735LCD_start, st7735LCD_UPDATE};
enum labKitLCD_States {labKitLCD_start, labKitLCD_IDLE, labKitLCD_SCORE, labKitLCD_WON, labKitLCD_LOST};
enum ballBrickCollision_States {ballBrickCollision_start, ballBrickCollision_UPDATE};
enum playerGameStatus_States {playerGameStatus_start, playerGameStatus_startScreen_ON, playerGameStatus_Play, playerGameStatus_POINTSCORED, playerGameStatus_WON, playerGameStatus_LOST, waitJoystickButtonRelease};
enum resetGame_States {resetGame_start, resetGame_INACTIVE, resetGame_ACTIVE};

int readJoystick_TickFct (int state) {
    unsigned int x_axis = ADC_read(PORTC1);
    unsigned int y_axis = ADC_read(PORTC0);
    switch(state) {
        case readJoystick_start:
            state = readJoystick_READ;
            break;

        case readJoystick_READ:
            //Neutral position ~(560, 550)
            joystick_global_y_axis = y_axis;
            joystick_global_x_axis = x_axis;
            break;

        default:
            state = readJoystick_start;
            break;
    }
    return state;
}

int paddleChangeBallDirection_TickFct (int state) {
    //Stop SM once game ends
    if(HALT_GAME){
        state = paddleChangeBallDirection_start;
        return state;
    }
    switch(state) {
        case paddleChangeBallDirection_start:
            //Check if Game is supposed to be running before do anything else
            if(!HALT_GAME){
                state = paddleChangeBallDirection_HOLD;
            }
            else {
                state = paddleChangeBallDirection_start;
            }
            break;

        case paddleChangeBallDirection_HOLD:
            //Check if Game is supposed to be running before do anything else
            if(!HALT_GAME){
                state = paddleChangeBallDirection_HOLD;
            }
            else {
                state = paddleChangeBallDirection_start;
            }
            
            if(ball_ypos >= (128-21)) {
                //Hit middle paddle region
                if((ball_xpos >= middle_PaddleRegion1) && (ball_xpos <= middle_PaddleRegion2)) {
                    hit_MiddlePaddle = 1;
                    hit_LeftPaddle = 0;
                    hit_RightPaddle = 0;
                    state = paddleChangeBallDirection_UPDATE;
                }
                //Hit left paddle region
                else if(((ball_xpos >= left_PaddleRegion1) || (ball_xpos <= left_PaddleRegion2)) && ((ball_xpos+10) < middle_PaddleRegion1)) {
                    hit_LeftPaddle = 1;
                    hit_MiddlePaddle = 0;
                    hit_RightPaddle = 0;
                    state = paddleChangeBallDirection_UPDATE;
                }
                //Hit right paddle region
                else if(((ball_xpos >= right_PaddleRegion1) || (ball_xpos <= right_PaddleRegion2)) && ((ball_xpos) > middle_PaddleRegion2)) {
                    hit_RightPaddle = 1;
                    hit_LeftPaddle = 0;
                    hit_MiddlePaddle = 0;
                    state = paddleChangeBallDirection_UPDATE;
                }
                //Paddle prolly did not contact ball
                else {
                    state = paddleChangeBallDirection_HOLD;
                }
            }
            //Hold until ball reaches paddle
            else {
                state = paddleChangeBallDirection_HOLD;
            }
            
            break;

        case paddleChangeBallDirection_UPDATE:
            //Set new ball direction once then return to hold
            if (hit_MiddlePaddle) {
                ball_xspeed = 0; //Travel straight up
            }
            else if (hit_LeftPaddle) {
                ball_xspeed = 3;
                ball_xdirection_flag = -1; //Force left
            }
            else if (hit_RightPaddle) {
                ball_xspeed = 3;
                ball_xdirection_flag =  1; //Force right
            }

            //Clear hit flags
            hit_LeftPaddle = hit_MiddlePaddle = hit_RightPaddle = 0;

            //Automatically go back to hold
            state = paddleChangeBallDirection_HOLD;
            break;
             
        default:
            state = paddleChangeBallDirection_start;
            break;
    }
    return state;
}

int st7735LCD_TickFct (int state) {
    //Stop SM once game ends
    if(HALT_GAME){
        state = st7735LCD_start;
        return state;
    }
    
    unsigned char old_ball_xpos;
    unsigned char old_ball_ypos;

    switch(state) {
        case st7735LCD_start:
            //Check if Game is supposed to be running before do anything else
            if(!HALT_GAME){
                state = st7735LCD_UPDATE;
            }
            else {
                state = st7735LCD_start;
            }
            break;

        case st7735LCD_UPDATE:
            //Check if Game is supposed to be running before do anything else
            if(!HALT_GAME){
                state = st7735LCD_UPDATE;
            }
            else {
                state = st7735LCD_start;
            }

            //Save current ball (x,y) to old ball (x,y)
            old_ball_xpos = ball_xpos;
            old_ball_ypos = ball_ypos;

            //Every Tick check if need to update the ball direction to keep ball within LCD 128x128 screen
            if(ball_xpos <= 6) {
                //Change direction that ball travels for x_axis
                ball_xdirection_flag *= -1;
            }
            //Optimal distance from top of screen (Less than ball y_speed to minimize bugs when ball goes faster)
            else if (ball_ypos <= ball_yspeed) {
                //Change direction that ball travels for y_axis
                ball_ydirection_flag *= -1;
            }
            else if(ball_xpos >= (129-13)) {
                //Change direction that ball travels for x_axis
                ball_xdirection_flag *= -1;
            }
            //Optimal distance from bottom of screen (ball plus paddle plus when ball y_speed increases)
            else if (ball_ypos >= (128-21)) { 
                //Change direction that ball travels for y_axis
                ball_ydirection_flag *= -1;
            }
            
            //Update ball position
            ball_xpos = ball_xpos + ((ball_xdirection_flag)*ball_xspeed);
            ball_ypos = ball_ypos + ((ball_ydirection_flag)*ball_yspeed);

            //Clear old ball
            clearST7735Window(old_ball_xpos-3,old_ball_xpos+12,old_ball_ypos-3,old_ball_ypos+12, 500);

            //Write new ball
            COL_SET(ball_xpos+(ball_xspeed*ball_xdirection_flag),ball_xpos+9+(ball_xspeed*ball_xdirection_flag));
            ROW_SET(ball_ypos+(ball_xspeed*ball_xdirection_flag),ball_ypos+9+(ball_xspeed*ball_xdirection_flag));
            DRAW_SPRITE(Game_Ball,100);
            
            //Clear old paddle
            clearST7735Window(paddle_xpos,paddle_xpos+36,120,123,148);

            //Write new paddle
            paddle_xpos = map_value(75,1020, 0, 129-37, joystick_global_x_axis);
            COL_SET(paddle_xpos,(paddle_xpos+36));
            ROW_SET(120,123);
            DRAW_SPRITE(Player_Paddle, 148);

            //Save paddle xpos to be used to compare w/ ball collision
            left_PaddleRegion1 = paddle_xpos;
            left_PaddleRegion2 = left_PaddleRegion1 + 12;
            middle_PaddleRegion1 = left_PaddleRegion2 + 1;
            middle_PaddleRegion2 = middle_PaddleRegion1 + 12;
            right_PaddleRegion1 = middle_PaddleRegion2 + 1;
            right_PaddleRegion2 = right_PaddleRegion1 + 12;
            
            break;
             
        default:
            state = st7735LCD_start;
            break;
    }
    return state;
}

int labKitLCD_TickFct (int state) {
    //Convert global player score to string that can be outputted to LCD
        //"%d" is format specifier that inteprets val as an integer in decimal form
    sprintf(scoreBuffer, "%d", playerScore);
    switch(state) {
        case labKitLCD_start:
            if(!HALT_GAME) {
                lcd_clear();
                state = labKitLCD_SCORE;
            }
            else if(gameLost && !START_GAME) {
                lcd_clear();
                state = labKitLCD_LOST;
            }
            else if(gameWon && !START_GAME) {
                lcd_clear();
                state = labKitLCD_WON;
            }
            else {
                lcd_clear();
                state = labKitLCD_IDLE;
            }
            break;
        
        case labKitLCD_IDLE:
            lcd_goto_xy(0,0);
            lcd_write_str(Display16x2_Start);

            //Wait for game to begin
            if(!HALT_GAME) {
                lcd_clear();
                state = labKitLCD_SCORE;
            }
            else {
                state = labKitLCD_IDLE;
            }
            break;

        case labKitLCD_SCORE:
            //Write "Score:" on first row
            lcd_goto_xy(0,0);
            lcd_write_str(Display16x2_Score);
            
            //Display player's score on first row
            lcd_goto_xy(0,6);
            lcd_write_str(scoreBuffer);

            //State Transitions
            if(gameLost) {
                lcd_clear();
                state = labKitLCD_LOST;
            }
            else if(gameWon) {
                lcd_clear();
                state = labKitLCD_WON;
            }
            else if(resetGame) {
                lcd_clear();
                state = labKitLCD_IDLE;
            }
            else {
                state = labKitLCD_SCORE;
            }
            break;

        case labKitLCD_LOST:
            //You Lost Text on first row
            lcd_goto_xy(0,0);
            lcd_write_str(Display16x2_Lost);    

            //Write "Score:" on second row
            lcd_goto_xy(1,0);
            lcd_write_str(Display16x2_Score);

            //Display player score on second row
            lcd_goto_xy(1,6);
            lcd_write_str(scoreBuffer);

            //State transitions
            if(resetGame) {
                lcd_clear();
                state = labKitLCD_IDLE;
            }
            else {
                state = labKitLCD_LOST;
            }
            break;

        case labKitLCD_WON:
            //You Win!! Text on first row
            lcd_goto_xy(0,0);
            lcd_write_str(Display16x2_Won);    

            //Write "Score:" on second row
            lcd_goto_xy(1,0);
            lcd_write_str(Display16x2_Score);

            //Display player score on second row
            lcd_goto_xy(1,6);
            lcd_write_str(scoreBuffer);

            //State transitions
            if(resetGame) {
                lcd_clear();
                state = labKitLCD_IDLE;
            }
            else {
                state = labKitLCD_WON;
            }
            break;
        
        default:
            state = labKitLCD_start;
            break;
    }
    return state;
}

int ballBrickCollision_TickFct (int state) {
    //Stop SM once game ends 
    if(HALT_GAME) { 
        state = ballBrickCollision_start; 
        return state; 
    }

    //Ball brick collision variables
    int ballHitBrick_Index; //Brick Bound index
    bool hitFromTopOrBottom; //Ball hit top or bottom of brick (generalized to flip y direction)
    bool hitFromRightOrLeft; //Ball hit right or left of brick (generalized to flip x direction)
    int currBallLeft;   //Current Ball shape
    int currBallRight;
    int currBallTop;
    int currBallBottom;
    int nextBallLeft;   //Next Ball shape
    int nextBallRight;
    int nextBallTop;
    int nextBallBottom;
    int brickTop;       //Brick Bounds
    int brickBottom;
    int brickLeft;
    int brickRight;
    bool foundIntersection; //Collision verified
    bool contactOnTop;  //Help determine which direction the ball contacted the brick
    bool contactOnBottom;
    bool contactOnRight;
    bool contactOnLeft;
    int remove_brickTop; //Brick bounds when removing a brick
    int remove_brickBottom;
    int remove_brickLeft;
    int remove_brickRight;


    switch (state) {
        case ballBrickCollision_start:
            //Check if Game is supposed to be running before do anything else 
            if(!HALT_GAME) { 
                state = ballBrickCollision_UPDATE; 
            } 
            else { 
                state = ballBrickCollision_start; 
            } 
            break;

        case ballBrickCollision_UPDATE: {
            //Check if Game is supposed to be running before do anything else 
            if(!HALT_GAME) { 
                state = ballBrickCollision_UPDATE; 
            } 
            else { 
                state = ballBrickCollision_start; 
            } 

            //Reinitialize vars every tick to prevent random bricks from being deleted without collision
            ballHitBrick_Index = -1; //BrickBounds index range from 0 to 24
            hitFromTopOrBottom = false; //Flip y direction of ball
            hitFromRightOrLeft = false; //Flip x direction of ball

            //Calculate the ball's current & next position
            currBallLeft = ball_xpos;
            currBallRight = ball_xpos + 9;
            currBallTop = ball_ypos;
            currBallBottom = ball_ypos + 9;

            nextBallLeft = ball_xpos + (ball_xdirection_flag * ball_xspeed);
            nextBallRight = ball_xpos + (ball_xdirection_flag * ball_xspeed) + 9;
            nextBallTop = ball_ypos + (ball_ydirection_flag * ball_yspeed);
            nextBallBottom = ball_ypos + (ball_ydirection_flag * ball_yspeed) + 9;

            //Iterate through all brick bounds to determine if ball's (x,y) will intersect 
            for (int i = 0; i < 25; i++) {
                //Check if brick is already gone at this index & go to next iteration if so
                if (!bricksExist[i]) {
                    continue;  
                }
                    
                //Grab the brick bounds from the array
                brickTop    = brickBoundary[i][0];
                brickBottom = brickBoundary[i][1];
                brickLeft   = brickBoundary[i][2];
                brickRight  = brickBoundary[i][3];

                //Check if brick and ball coords will match & go to next iteration if they are not the same (next ball position vs brick - Intersection)
                if ((nextBallRight > brickLeft) && (nextBallLeft < brickRight) && (nextBallBottom > brickTop) && (nextBallTop < brickBottom)) {
                    foundIntersection = 1;
                }
                else {
                    foundIntersection = 0;
                }

                if (!foundIntersection) {
                    continue;
                }

                //Store index where *verified intersection* with ball and brick occured and will be used later
                ballHitBrick_Index = i;

                //Determine how the ball hit the brick so can flip correct x or y direction
                contactOnTop = (currBallBottom <= brickTop) && (nextBallBottom > brickTop);
                contactOnBottom = (currBallTop >= brickBottom) && (nextBallTop < brickBottom);
                contactOnRight = (currBallLeft >= brickRight) && (nextBallLeft < brickRight);
                contactOnLeft = (currBallRight <= brickLeft) && (nextBallRight > brickLeft);

                // Prefer vertical resolves if we crossed top/bottom; otherwise horizontal.
                if (contactOnTop || contactOnBottom) {
                    hitFromTopOrBottom = true;
                    hitFromRightOrLeft = false;
                } 
                else if (contactOnRight || contactOnLeft) {
                    hitFromRightOrLeft = true;
                    hitFromTopOrBottom = false;
                }

                //Stop iterating since found verified collision & updated all necessary collision variables correctly
                break; 
            }

            //Apply new changes only if the ball actually made contact with a brick
            if (ballHitBrick_Index >= 0) {
                //Remove brick visually
                remove_brickTop    = brickBoundary[ballHitBrick_Index][0];
                remove_brickBottom = brickBoundary[ballHitBrick_Index][1];
                remove_brickLeft   = brickBoundary[ballHitBrick_Index][2];
                remove_brickRight  = brickBoundary[ballHitBrick_Index][3];

                Set_Brick(Black_Brick, remove_brickTop, remove_brickBottom, remove_brickLeft, remove_brickRight);

                //Remove brick from bool array
                bricksExist[ballHitBrick_Index] = 0;

                //Change direction ball will travel now zcorrectly
                if (hitFromTopOrBottom) {
                    ball_ydirection_flag *= -1;
                } 
                else if (hitFromRightOrLeft) {
                    ball_xdirection_flag *= -1;
                }

                bricksDestroyedCounter++;
            }

            saveGame();

            //Update Bricks being displayed so ball's clear window does not draw over brick color
            Bricks_REINIT();

            state = ballBrickCollision_UPDATE;
            break;
        }

        default:
            state = ballBrickCollision_start;
            break;
    }
    return state;
}

int playerGameStatus_TickFct (int state) {
    bool joystickButtonPressed = !((PINC >> 2) & 0x1); //active low button

    switch(state) {
        case playerGameStatus_start:
            //Check where to which state to start in 
            if(START_GAME) {
                //Set enitre background of LCD to white
                setWhiteBackground();
                state = playerGameStatus_startScreen_ON;
            }
            else if(gameWon) {
                state = playerGameStatus_WON;
            }
            else if(gameLost) {
                state = playerGameStatus_LOST;
            }
            else {
                state = playerGameStatus_Play;
            }
            break;

        case playerGameStatus_Play:
            HALT_GAME = false;
            gameLost = false;
            gameWon = false;
            START_GAME = false;

            //Game Lost
            if(ball_ypos >= (128-21)) {
                if(((paddle_xpos+36) < ball_xpos) || (paddle_xpos > (ball_xpos+9))) {
                    HALT_GAME = 1;

                    //Set enitre background of LCD to white
                    setWhiteBackground();
                    state = playerGameStatus_LOST;
                }
            }
            //Game Won
            else if(bricksDestroyedCounter >= 25) {
                HALT_GAME = 1;

                setWhiteBackground(); //Set enitre background of LCD to white
                playerScore += 100;    //Give last 100 points to player
                state = playerGameStatus_WON;
            }
            //Point scored since last tick
            else if (bricksDestroyedCounter > prevBricksDestroyed) {
                state = playerGameStatus_POINTSCORED;
            }
            //Reset Game
            else if(resetGame) {
                START_GAME = true;
                state = playerGameStatus_start;
            }
            else {
                state = playerGameStatus_Play;
            }

            //Update counter
            prevBricksDestroyed = bricksDestroyedCounter;

            saveGame();
            break;

        case playerGameStatus_startScreen_ON:
            HALT_GAME = true;

            ROW_SET(39,88);
            COL_SET(15,114);
            DRAW_SPRITE(Start_Screen, 5000);

            //Wait for joystick button to be pressed and released to start game
            if(joystickButtonPressed) {
                state = waitJoystickButtonRelease;
            }
            else {
                state = playerGameStatus_startScreen_ON;
            }
            break;

        case playerGameStatus_POINTSCORED:
            //5 Bricks broken increase speed
            if(bricksDestroyedCounter % 5 == 0) {
                ball_yspeed++; 
            }

            playerScore+=100;
            saveGame();

            state = playerGameStatus_Play;
            break;

        case playerGameStatus_WON:
            //Write on 16x2 You Win!! & stop rest of game
            HALT_GAME = 1;
            gameWon = true;
            
            saveGame();

            ROW_SET(39,88);
            COL_SET(15,114);
            DRAW_SPRITE(Game_Over, 5000);

            //Wait for game to be reset
            if(resetGame) {
                state = playerGameStatus_start;
            }
            else {
                state = playerGameStatus_WON;
            }
            break;

        case playerGameStatus_LOST:
            //Write on 16x2 You Lost & stop rest of game
            HALT_GAME = 1;
            gameLost = true;
            saveGame();

            ROW_SET(39,88);
            COL_SET(15,114);
            DRAW_SPRITE(Game_Over, 5000);

            //Wait for game to be reset
            if(resetGame) {
                state = playerGameStatus_start;
            }
            else {
                state = playerGameStatus_LOST;
            }
            break;

        case waitJoystickButtonRelease:
            if(joystickButtonPressed) {
                state = waitJoystickButtonRelease;
            }
            else {
                //Set enitre background of LCD to black (129*130) before starting game
                clearST7735Window(0,129,0,128,16770); 
                //Display Initial Bricks
                Bricks_INIT();
                state = playerGameStatus_Play;
            }
            break;

        default: 
            state = playerGameStatus_start;
            break;
    }
    return state;
}

int resetGame_TickFct (int state) {
    bool buttonPressed = (PINC>>3) & 0x01; //PC4
    switch(state) {
        case resetGame_start:
            state = resetGame_INACTIVE;
            break;

        case resetGame_INACTIVE:
            resetGame = 0;
            if(buttonPressed) {
                state = resetGame_ACTIVE;
            }
            else {
                state = resetGame_INACTIVE;
            }
            break;

        case resetGame_ACTIVE: {
            HALT_GAME = 1;
            resetGame = 1;
            gameWon = 0;
            gameLost = 0;
            START_GAME = 1;

            //Reinitialize player score to 0
            playerScore = 0;
            //Reinitialize number of bricks destroyed
            bricksDestroyedCounter = 0;
            //Reinitialize bricks
            for(int i = 0; i < 25; i++) {
                bricksExist[i] = true;
            }
            //Reinitialize to start ball above paddle
            ball_xpos = 60;
            ball_ypos = 100;
            //Reinitilize other ball mechanics
            ball_xspeed = 0; 
            ball_yspeed = 5; 
            ball_xdirection_flag = -1;
            ball_ydirection_flag = 1; 
            hit_RightPaddle = 0; 
            hit_MiddlePaddle = 1;
            hit_LeftPaddle = 0;

            saveGame();

            //State transition
            if(buttonPressed) {
                state = resetGame_ACTIVE;
            }
            else {
                state = resetGame_INACTIVE;
            }
            break;
        }
        default:
            state = resetGame_start;
            break;
    }
    return state;
}

int main(void) {
    //Initialize all inputs and ouputs
    DDRC = 0x00;
    PORTC = 0xFF;

    DDRD = 0xFF;
    PORTD = 0x00;

    DDRB = 0xFF;
    PORTB = 0x00;

    //Initializations for hardware components
        //Initializes ADC for joystick
        ADC_init(); 
        //Initializes 16x2 Lab Kit LCD
        lcd_init();
        lcd_clear();
        //Initializes st7735LCD
        SPI_INIT();
        hardware_RESET();
        st7735_INIT();

    //Initializations for Game
        //Set white background initially
        setWhiteBackground();

        //Initialize joystick position vars to neutral position
        joystick_global_x_axis = 570;
        joystick_global_y_axis = 560;

        //Initialize to start ball above paddle
        ball_xpos = 60;
        ball_ypos = 100;

        //Load game from EEPROM flash memory
        loadGame();

    //Initialize tasks
    tasks[0].period = TASK1_PERIOD;
    tasks[0].state = readJoystick_start;
    tasks[0].elapsedTime = 0;
    tasks[0].TickFct = &readJoystick_TickFct;

    tasks[1].period = TASK2_PERIOD;
    tasks[1].state = paddleChangeBallDirection_start;
    tasks[1].elapsedTime = 0;
    tasks[1].TickFct = &paddleChangeBallDirection_TickFct;

    tasks[2].period = TASK3_PERIOD;
    tasks[2].state = st7735LCD_start;
    tasks[2].elapsedTime = 0;
    tasks[2].TickFct = &st7735LCD_TickFct;

    tasks[3].period = TASK4_PERIOD;
    tasks[3].state = labKitLCD_start;
    tasks[3].elapsedTime = 0;
    tasks[3].TickFct = &labKitLCD_TickFct;

    tasks[4].period = TASK5_PERIOD;
    tasks[4].state = ballBrickCollision_start;
    tasks[4].elapsedTime = 0;
    tasks[4].TickFct = &ballBrickCollision_TickFct;

    tasks[5].period = TASK6_PERIOD;
    tasks[5].state = playerGameStatus_start;
    tasks[5].elapsedTime = 0;
    tasks[5].TickFct = &playerGameStatus_TickFct;

    tasks[6].period = TASK7_PERIOD;
    tasks[6].state = resetGame_start;
    tasks[6].elapsedTime = 0;
    tasks[6].TickFct = &resetGame_TickFct;

    TimerSet(GCD_PERIOD);
    TimerOn();

    while (1) {}

    return 0;
}