#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD; // stop watchdog timer
	P1DIR |= BIT0 + BIT6 + BIT4;
	P1OUT |= BIT4;

	while(1){
	    // to turn on LED1
	    P1OUT |= BIT0;
	    P1OUT &= ~BIT6;
	    __delay_cycles(100000);

	    // to turn on LED2
	    P1OUT |= BIT6;
	    P1OUT &= ~BIT0;
	    __delay_cycles(100000);
	}


	
	return 0;
}
