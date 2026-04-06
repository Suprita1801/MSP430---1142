#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer


    P2DIR &=~ BIT0; // P2.0 as input
	P2REN |= BIT0; // P2.0 pull-up or Pull-doen resistor enabled
	P2OUT &=~ BIT0; // P2.0 Pull down

	while(1){
//	    __no_operation();

	}
	

	return 0;
}
