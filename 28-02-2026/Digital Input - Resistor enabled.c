#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	
	P2DIR &= ~BIT1;
	P2REN |= BIT1; //Resistor enabled
	P1DIR |= BIT0 + BIT6;
	P2OUT |= BIT1; //Pull up transistor

	while(1){
	    if((P2IN & BIT1) == BIT1){
	        P1OUT |= BIT0;
	        __delay_cycles(100000);
	        P1OUT &= ~BIT0;
	        __delay_cycles(100000);

	    }
	    else{
	        P1OUT |= BIT6;
	        __delay_cycles(100000);
	        P1OUT &= ~BIT6;
	        __delay_cycles(!00000);
	    }
	}



//	 P2DIR &=~ BIT1;
//	 P1DIR |= (BIT0 + BIT6);
//	    while(1){
//	        if((P2IN&BIT1)==BIT1){
//	            P1OUT |= BIT0;
//	            __delay_cycles(100000);
//	            P1OUT &=~ BIT0;
//	            __delay_cycles(100000);
//	        }
//	        else{
//	            P1OUT |= BIT6;
//	            __delay_cycles(100000);
//	            P1OUT &=~ BIT6;
//	            __delay_cycles(100000);
//	        }
//	    }

	return 0;
}
