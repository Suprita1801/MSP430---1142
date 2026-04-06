
#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{

    WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	
    P1DIR |= BIT0; //P1.0 as output
    P2DIR &= ~BIT0; //P2.0 as input
    P2REN |= BIT0; //P2.0 as pull up or pull down resistor
    P2OUT &= ~BIT0; //P2.0 as pull down resistor

    while(1){
        if((P2IN & BIT0) == BIT0){
            __delay_cycles(100000); //0.1s delay
        }
        else{

            __delay_cycles(1000000); //1s delay
        }

        P1OUT ^= BIT0;
    }

    return 0;
}
