#include <msp430.h>

#define ONE_SEC             1000000
#define CARDIO_MAX          900        // 15 min
#define CARDIO_WARN         720        // 12 min
#define UPPER_TRAIN_MAX     300        // 5 min
#define UPPER_TRAIN_WARN    240        // 4 min
#define LEG_TRAIN_MAX       300        // 5 min
#define LEG_TRAIN_WARN      240        // 4 min
#define REST_TIME           60         // 1 min rest
#define REST_BEEP_DURATION  30         // long beep for 30s on exit

#define DISP_MODE           0
#define DISP_MINUTES        1

#define BCD_MASK (BIT2 + BIT3 + BIT4 + BIT5)

// LED pins
#define LED_RED    BIT0
#define LED_YELLOW BIT5
#define LED_GREEN  BIT6
#define LED_BLUE   BIT7

void shortBeep(void);
void longBeep(void);
void emergencyBeep(void);
void updateDisplay(void);
void allLEDsOff(void);

int time         = 0;
int inUse        = 0;
int mode         = 0;
int restMode     = 0;
int restTimer    = 0;
int displayState = DISP_MODE;

int main(void){
    WDTCTL = WDTPW | WDTHOLD;

    // LEDs as output
    P1DIR |= (LED_RED + LED_YELLOW + LED_GREEN + LED_BLUE);
    allLEDsOff();

    // Buzzer
    P1DIR |= BIT2;

    // Entry button P1.3 — pull-up
    P1DIR &= ~BIT3;
    P1REN |= BIT3;
    P1OUT |= BIT3;

    // Exit button P1.4 — pull-down
    P1DIR &= ~BIT4;
    P1REN |= BIT4;
    P1OUT &= ~BIT4;

    // Emergency button P1.1 — pull-up
    P1DIR &= ~BIT1;
    P1REN |= BIT1;
    P1OUT |= BIT1;

    // Mode button P2.0 — pull-up
    P2DIR &= ~BIT0;
    P2REN |= BIT0;
    P2OUT |= BIT0;

    // BCD output P2.2-P2.5
    P2DIR |= BCD_MASK;
    P2OUT &= ~BCD_MASK;

    // Port 1 interrupts
    P1IFG = 0;
    P1IE  |= (BIT1 + BIT3 + BIT4);
    P1IES |= (BIT1 + BIT3);    // falling edge: entry + emergency
    P1IES &= ~BIT4;             // rising edge: exit

    // Port 2 interrupt — mode button
    P2IFG = 0;
    P2IE  |= BIT0;
    P2IES |= BIT0;              // falling edge

    __bis_SR_register(GIE);

    // Startup — 3 blinks then show mode 0
    int j;
    for(j = 0; j < 3; j++){
        P1OUT |= LED_RED;
        __delay_cycles(500000);
        P1OUT &= ~LED_RED;
        __delay_cycles(500000);
    }

    displayState = DISP_MODE;
    updateDisplay();             // show 0 on display at startup

    while(1){

        // Active session
        if(inUse){
            __delay_cycles(ONE_SEC);
            time++;

            int MAX_TIME, WARN_TIME;
            switch(mode){
                case 0:
                    MAX_TIME  = CARDIO_MAX;
                    WARN_TIME = CARDIO_WARN;
                    break;
                case 1:
                    MAX_TIME  = UPPER_TRAIN_MAX;
                    WARN_TIME = UPPER_TRAIN_WARN;
                    break;
                case 2:
                    MAX_TIME  = LEG_TRAIN_MAX;
                    WARN_TIME = LEG_TRAIN_WARN;
                    break;
                default:
                    MAX_TIME  = CARDIO_MAX;
                    WARN_TIME = CARDIO_WARN;
                    break;
            }

            displayState = DISP_MINUTES;
            updateDisplay();

            if(time >= MAX_TIME){
                // Overtime — Red ON, long beep every second
                allLEDsOff();
                P1OUT |= LED_RED;
                longBeep();
            }
            else if(time >= WARN_TIME){
                // Warning — Yellow ON + Green blinks, short beep
                allLEDsOff();
                P1OUT |= LED_YELLOW;
                P1OUT ^= LED_GREEN;   // blink green each second
                shortBeep();
            }
            else{
                // Safe zone — all LEDs off, silent
                allLEDsOff();
            }
        }

        // Rest period
        if(restMode){
            __delay_cycles(ONE_SEC);
            restTimer++;

            // First 30s — Blue ON + long beep to signal rest
            if(restTimer <= REST_BEEP_DURATION){
                P1OUT |= LED_BLUE;
                longBeep();
            }
            // 30s–60s — Blue ON, silent
            else{
                P1OUT |= LED_BLUE;
            }

            // Rest done — signal next user
            if(restTimer >= REST_TIME){
                restMode  = 0;
                restTimer = 0;

                allLEDsOff();
                P1OUT |= LED_RED;        // Red ON — next user enter

                displayState = DISP_MODE;
                updateDisplay();            // shows 0/1/2

                shortBeep();
                shortBeep();
            }
        }
    }
}

//Port 1 ISR
#pragma vector = PORT1_VECTOR
__interrupt void Port_1(void){

    __delay_cycles(500000);    // debounce

    if((P1IFG & BIT3) == BIT3){          // ENTRY
        inUse    = 1;
        time     = 0;
        restMode = 0;

        allLEDsOff();
        P1OUT |= LED_RED;                 // Red ON — session started
        displayState = DISP_MODE;
        updateDisplay();                  // briefly show mode on entry
        shortBeep();
        P1IFG &= ~BIT3;
    }
    else if((P1IFG & BIT4) == BIT4){    // EXIT
        inUse     = 0;
        time      = 0;
        restMode  = 1;
        restTimer = 0;

        displayState = DISP_MODE;   // clean state reset
        P2OUT &= ~BCD_MASK;         // blank display during rest

        allLEDsOff();
        P1OUT |= LED_BLUE;
        P1IFG &= ~BIT4;
    }
    else if((P1IFG & BIT1) == BIT1){     // EMERGENCY
        inUse    = 0;
        time     = 0;
        restMode = 0;

        P2OUT &= ~BCD_MASK;
        P1OUT |= (LED_RED + LED_YELLOW + LED_GREEN + LED_BLUE);
        emergencyBeep();
        P1IFG &= ~BIT1;
    }

    P1IFG = 0;
}

// Port 2 ISR — mode button
#pragma vector = PORT2_VECTOR
__interrupt void Port_2(void){

    __delay_cycles(500000);    // debounce

    if((P2IFG & BIT0) == BIT0){

        // Mode change only allowed when not in active session
        if(!inUse){
            mode = (mode + 1) % 3;       // cycle 0 → 1 → 2 → 0

            // Green LED flash — confirms mode change
            P1OUT |= LED_GREEN;
            __delay_cycles(200000);
            P1OUT &= ~LED_GREEN;

            // Show new mode on display
            displayState = DISP_MODE;
            updateDisplay();

            shortBeep();
        }

        P2IFG &= ~BIT0;
    }

    P2IFG = 0;
}

//all LEDs off
void allLEDsOff(void){
    P1OUT &= ~(LED_RED + LED_YELLOW + LED_GREEN + LED_BLUE);
}

//  Buzzer functions
void shortBeep(void){
    P1OUT |= BIT2;
    __delay_cycles(100000);
    P1OUT &= ~BIT2;
    __delay_cycles(100000);
}

void longBeep(void){
    // Split into 5 x 100ms instead of one 500ms block
    int i;
    for(i = 0; i < 5; i++){
        P1OUT |= BIT2;
        __delay_cycles(80000);
        P1OUT &= ~BIT2;
        __delay_cycles(20000);
    }
}
void emergencyBeep(void){
    int i;
    for(i = 0; i < 10; i++){
        P1OUT ^= BIT2;
        __delay_cycles(50000);
    }
}

//Display update
void updateDisplay(void){
    int val = 0;
    if(displayState == DISP_MODE){
        val = mode;                       // 0, 1, or 2
    } else {
        int MAX_TIME = (mode == 0) ? CARDIO_MAX :
                       (mode == 1) ? UPPER_TRAIN_MAX : LEG_TRAIN_MAX;
        int secsLeft = MAX_TIME - time;
        if(secsLeft < 0) secsLeft = 0;
        val = secsLeft / 60;
        if(val > 9) val = 9;
    }
    P2OUT &= ~BCD_MASK;
    P2OUT |= (val << 2);
}
