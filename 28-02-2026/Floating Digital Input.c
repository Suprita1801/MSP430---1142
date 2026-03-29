#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	
	P1DIR &= ~BIT5;
	P1REN |= BIT5;
//	P1OUT |= BIT5; //Pull up transistor
	P1OUT &= ~BIT5; //Pull down transistor
	P1DIR |= BIT0 + BIT6;

	while(1){

	    if((P1IN & BIT5) == BIT5){
	        P1OUT |= BIT0;
	        P1OUT &= ~BIT6;
	    }
	    else{
	        P1OUT |= BIT6;
	        P1OUT &= ~BIT0;
	    }
	}

	return 0;
}
