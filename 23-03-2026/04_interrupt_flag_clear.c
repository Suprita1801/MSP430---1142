#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;   // stop watchdog timer

    P1DIR |= (BIT0 + BIT6);
    P1OUT |= BIT0;
    P1OUT &= ~BIT6;
    P1DIR &= ~BIT4;
    P1REN |= BIT4;
    P1OUT &= ~BIT4;
    P1IE |= BIT4; // P1.4 -> Interrupt enabled
    P1IFG &= ~BIT4; // P1.4 -> Clears if any pre-existing flag present
    __bis_SR_register(GIE); //General Interrupt enabled

    while(1){
        P1OUT ^= (BIT0 + BIT6);
        __delay_cycles(2000000);

    }
}

// Hardware Interrupt Sub-routine

#pragma vector = PORT1_VECTOR
__interrupt void Port_1(void)
{
    char i;
    if((P1IN & BIT4) == BIT4){
        for(i =0 ; i < 10 ; i++){
            P1OUT ^= (BIT0 + BIT6);
            __delay_cycles(100000);
        }

        P1IFG &= ~BIT4; //No interrupt is pending -> move out of hardware interrupt

    }
}
