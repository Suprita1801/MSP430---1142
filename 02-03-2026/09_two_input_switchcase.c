#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	
	P1DIR |= (BIT0 + BIT6);
	P1OUT |= BIT0; // P1.0 -> High
	P1OUT &= ~BIT6; // P1.6 -> Low
	P1DIR &= ~(BIT3 + BIT4); // P1.4 -> As Input
	P1REN |= (BIT3 + BIT4);
	P1OUT |= BIT3; // Pull up enabled
	P1OUT &= ~BIT4; // Pull down enabled

	while(1){
	    switch(P1IN & (BIT3+BIT4)){
	            case BIT3:
	                __delay_cycles(3000000);
	                break;
	            case 0:
	                __delay_cycles(1000000);
	                break;
	            case (BIT3 + BIT4):
	                    __delay_cycles(500000);
	                    break;
	            case BIT4:
	                __delay_cycles(100000);
	                break;
	    }

	    P1OUT ^= (BIT0 + BIT6);

	}




	return 0;
}
