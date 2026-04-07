#include <msp430.h> 
int count;
int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;   // stop watchdog timer

    P1DIR |= BIT0;
    P1OUT |= BIT0;
    P1DIR &= ~BIT4;
    P1REN |= BIT4;
    P1OUT &= ~BIT4;
    P1IES &= ~BIT4; // low to high IES - Interrupt edge selection
//    P1IES |= BIT4; // high to low
    P1IE |= BIT4; // P1.4 -> Interrupt enabled
    __bis_SR_register(GIE); //General Interrupt enabled
    P1IFG &= ~BIT4;

    count = 0;
    while(1); //Infinite loop to check no of counts
}

// Hardware Interrupt Sub-routine

#pragma vector = PORT1_VECTOR
__interrupt void Port_1(void)
{
    count++; //Count usualy differs because internally when the switch is pressed it undergoes so many jumps
    P1IFG &= ~BIT4;
}
