#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	
	char i;

	P1DIR |= (BIT0 + BIT6);
	P1OUT |= BIT0;
	P1OUT &= ~BIT6;
	P1DIR &= ~BIT4;
	P1REN |= BIT4;
	P1OUT &= ~BIT4;

//	Interrupt

	while(1){
	    P1OUT ^= (BIT0 + BIT6);
	    for(i = 0; i < 10 ; i++){
	       check();
	       __delay_cycles(200000);

	    }

	}
}

// Polling


void check(){

    char i;

    if((P1IN & BIT4) == BIT4){
        for(i = 0 ; i < 10 ; i++){
            P1OUT ^= (BIT0 + BIT6);
            __delay_cycles(100000);
        }

    }
}
