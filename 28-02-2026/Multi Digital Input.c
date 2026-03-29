#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	
	P2DIR &= ~(BIT1 + BIT2);
	P2REN |= (BIT1 + BIT2);
	P2OUT |= (BIT1 + BIT2);
	P1DIR |= (BIT0 + BIT6);

	while(1){

	    if((P2IN & BIT1) == 0 && (P2IN & BIT2) == 0){
	        P1OUT &= ~(BIT0 + BIT6);
	    }
	    else if((P2IN & BIT1) == BIT1 && (P2IN & BIT2) == 0){
	        P1OUT |= BIT0;
	        P1OUT &= ~BIT6;
	    }
	    else if((P2IN & BIT1) == 0 && (P2IN & BIT2) == BIT2){
	        P1OUT &= ~BIT0;
	        P1OUT |= BIT6;
	    }
	    else if((P2IN & BIT1) == BIT1 && (P2IN & BIT2) == BIT2){
	        P1OUT |= (BIT0 + BIT6);
	    }
	}

	return 0;
}
