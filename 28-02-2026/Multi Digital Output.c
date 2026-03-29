#include <msp430.h>


/**
 * main.c
 */
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	P1DIR |= BIT0 + BIT6;
	P2DIR |= BIT3;
	P2OUT |= BIT3;

	while(1){
	    P1OUT |= BIT0;
	    P1OUT &= ~BIT6;
	    __delay_cycles(100000);

	    P1OUT |= BIT6;
	    P1OUT &= ~BIT0;
	    __delay_cycles(100000);
	}

	return 0;
}
