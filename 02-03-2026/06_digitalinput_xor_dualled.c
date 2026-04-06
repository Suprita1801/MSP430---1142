#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	
	P1DIR |= (BIT0 + BIT6);
	P2DIR &= ~BIT0;
	P2REN |= BIT0;
	P2OUT &= ~BIT0; //Pull down enabled

	while(1){
	    if((P2IN & BIT0) == BIT0)
	        __delay_cycles(100000); // 0.1 sec
	    else
	        __delay_cycles(1000000); // 1 sec

	    P10UT ^= (BIT0 + BIT6);
	}

	return 0;
}
