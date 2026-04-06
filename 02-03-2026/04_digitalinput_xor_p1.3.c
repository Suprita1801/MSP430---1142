#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	
	P1DIR |= (BIT0 + BIT6);
//	P1OUT |= BIT0; // P1.0 -> High
//	P1OUT &= ~BIT6; // P1.6 -> Low
	P1DIR &= ~BIT3;
	P1REN |= BIT3;
	P1OUT |= BIT3;

	while(1){

	    if((P1IN & BIT3) == BIT3){
	        __delay_cycles(200000);
	    }
	    else{
	        __delay_cycles(2000000);
	    }

	    P1OUT ^= (BIT0 + BIT6);
	}

	return 0;
}
