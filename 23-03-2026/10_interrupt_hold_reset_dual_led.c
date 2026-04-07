#include <msp430.h> 
int count;
int main(void)
{

    WDTCTL = WDTPW | WDTHOLD;   // stop watchdog timer

    P1DIR |= (BIT0 + BIT6);
    P1OUT &=  ~(BIT0 + BIT6);
    P1DIR &= ~BIT4;
    P1REN |= BIT4;
    P1OUT &= ~BIT4;
    P1IES &= ~BIT4; // low to high IES - Interrupt edge selection
//    P1IES |= BIT4; // high to low
    P1IFG &= ~BIT4; // clear the flag
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
    char ct; // to reset the count after the condition of ct satsified

    count++;
    ct = 0;

    while((P1IN & BIT4) == BIT4){
        P1OUT |= BIT0; //If button pressed, led blinks
        __delay_cycles(100000);
        P1OUT &= ~BIT0;
        ct++; // Timer count is incremented

        if(ct >= 30){
            count = 0; // Count set to zero
            P1OUT |= BIT6; // Indicates count is set to zero
            __delay_cycles(500000);
            P1OUT &= ~BIT6; // after delay if 0.5 sec led is switched off
            break;
        }
    }
        P1IFG &= ~BIT4;
}
