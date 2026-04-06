#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	
    P1DIR |= BIT0; // P1.0 as output
    P2DIR &= ~ BIT0; // P2.0 as input
    P2REN |= BIT0; // Pull up or pull down resistor enabled
    P2OUT &=~ BIT0; // Pull DOWN resistor enabled (P2.0)

    while(1){
        if((P2IN & BIT0) == BIT0){
            P1OUT |= BIT0; // LED1 no glow
        }
        else{
            P1OUT &= ~BIT0; // LED1 glows
        }

    }
    return 0;
}
