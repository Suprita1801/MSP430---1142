#include <msp430.h> 
#define SPACE   6 // Total park lots
int space;
int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;   // stop watchdog timer
    P1DIR &= ~(BIT3 + BIT4); //P1.3 -> Entry Point of Parking & P1.4 -> Exit Point of Parking
    P1DIR |= (BIT0 + BIT6);
    P1REN |= (BIT3 + BIT4);
    P1OUT |= BIT3; // Pull up transistor
    P1OUT &= ~BIT4; //Pull down transistor
    P1IE |= (BIT3 + BIT4); // Interrupt enabled for P1.3 & P1.4
    P1IES |= BIT3; // Interrupt Edge Select -> High to low
    P1IES &= ~BIT4; // Interrupt Edge Select -> Low to high
    __bis_SR_register(GIE); //Global interrupt ON
    P1IFG &= ~(BIT3 + BIT4); //Reset Interrupt Flag

    space = SPACE;
    while(1){
        // To check the availability of the space
        if(space > 0){
            P1OUT |= BIT6;
            P1OUT &= ~BIT0;
        }
        else{
            P1OUT |= BIT0;
            P1OUT &= ~BIT6;
        }
//        if((P1IN & (BIT3 + BIT4)) == BIT4){
//            __delay_cycles(500000);
//            P1IFG &= ~(BIT3 + BIT4); // Make sure the flag is been reset before enters the interrupt
//            space = 0;
//        }
    }
}
/*
 * To know which button has been pressed while the car enters,
 * we have set up the interrupt flag. So when car enters,
 * the space decreases followed by flag of P1.3 reset
 * If car leaves the space increase with flag of P1.4 reset */

#pragma vector = PORT1_VECTOR
__interrupt void Port_1(void){

    __delay_cycles(500000); //Make sure the user press the two button relaxly by 0.5 sec otherwise it enters into the below mentioned condition

    if((P1IFG & (BIT3 + BIT4)) == (BIT3 + BIT4)){
        __delay_cycles(500000); //Flag reset for two button in the interrupt
        P1IFG &= ~(BIT3 + BIT4);
        space = 0;
    }

    if((P1IFG & BIT3) == BIT3){
         // Car entry
//        __delay_cycles(500000);  // Since delay is mentioned at the beginning not need delay inside this condition. Make sure initialisation at the start makes the process faster
        if(space > 0) //To make sure negative count is not taken
            space--;
        P1IFG &= ~BIT3;
    }
    else if((P1IFG & BIT4) == BIT4){
        // Car exits
//        __delay_cycles(500000); // Since delay is mentioned at the beginning not need delay inside this condition. Make sure initialisation at the start makes the process faster
        if(space < SPACE) // To make sure it doesn't exceed more than 6
            space++;
        P1IFG &= ~BIT4;
    }
}
