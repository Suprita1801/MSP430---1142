#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	P1DIR |= BIT0; // P1.0 -> Output
	P2DIR &= ~BIT0; // P2.0 -> Input
	P2REN |= BIT0; // Pullup or Pulldown transistor enabled
	P2OUT |= BIT0; // Pull up transistor enabled

	    while(1){
	        if((P2IN & BIT0) == BIT0){
	            P1OUT |= BIT0;
	        }

	        else{
	            P1OUT &= ~BIT0;
	        }
	    }

	
	return 0;
}
