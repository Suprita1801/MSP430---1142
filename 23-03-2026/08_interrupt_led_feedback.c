#include <msp430.h> 
int count;
int main(void)
{

    WDTCTL = WDTPW | WDTHOLD;   // stop watchdog timer

    P1DIR |= BIT0;
    P1OUT &=  ~BIT0; //P1.0 -> as input to indicate the button pressed
    P1DIR &= ~BIT4;
    P1REN |= BIT4;
    P1OUT &= ~BIT4;
    P1IES &= ~BIT4; // low to high IES - Interrupt edge selection
//    P1IES |= BIT4; // high to low
    P1IE |= BIT4; // P1.4 -> Interrupt enabled
    __bis_SR_register(GIE); //General Interrupt enabled

    count = 0;
    while(1); //Infinite loop to check no of counts
}

// Hardware Interrupt Sub-routine

#pragma vector = PORT1_VECTOR
__interrupt void Port_1(void)

// If condition to avoid internal jumps
{
    if((P1IN & BIT4) == BIT4){
        count++;
        P1OUT |= BIT0; //If button pressed, led blinks
        __delay_cycles(500000);
        P1OUT &= ~BIT0; // back to 0 -> reset and add 1 to count
        P1IFG &= ~BIT4; // Move out of hardware interrupt

    }


}
