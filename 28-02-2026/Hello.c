#include <msp430.h> 


/**
 * main.c
 */
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	P1DIR = 0b01000001;
	P1OUT = 0b01000001;
	return 0;
}
