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
	    switch(P2IN & (BIT1 + BIT2)){
	    case 0:
	        P1OUT &= ~(BIT0 + BIT6);
	        break;
	    case BIT1:
	        P1OUT |= BIT0;
	        P1OUT &= ~BIT6;
	        break;
	    case BIT2:
	        P1OUT |= BIT6;
	        P1OUT &= ~BIT0;
	        break;
	    case (BIT1 + BIT2):
	            P1OUT |= (BIT0 + BIT6);
	            break;
	    }
	}


	return 0;
}
