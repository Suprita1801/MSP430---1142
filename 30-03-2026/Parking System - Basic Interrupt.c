#include <msp430.h> 
int space; // Total park lots
int main(void)
{
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	P1DIR &= ~(BIT3 + BIT4); //P1.3 -> Entry Point of Parking & P1.4 -> Exit Point of Parking
	P1REN |= (BIT3 + BIT4);
	P1OUT |= BIT3; // Pull up transistor
	P1OUT &= ~BIT4; //Pull down transistor
	P1IE |= (BIT3 + BIT4); // Interrupt enabled for P1.3 & P1.4
	P1IES |= BIT3; // Interrupt Edge Select -> High to low
	P1IES &= ~BIT4; // Interrupt Edge Select -> Low to high
	__bis_SR_register(GIE); //Global interrupt ON
	P1IFG &= ~(BIT3 + BIT4); //Reset Interrupt Flag

	space = 6;
	while(1);
}
/*
 * To know which button has been pressed while the car enters,
 * we have set up the interrupt flag. So when car enters,
 * the space decreases followed by flag of P1.3 reset
 * If car leaves the space increase with flag of P1.4 reset */

#pragma vector = PORT1_VECTOR
__interrupt void Port_1(void){

    if((P1IFG & BIT3) == BIT3){
        __no_operation(); // Car entry
        space--;
        P1IFG &= ~BIT3;
    }
    else if((P1IFG & BIT4) == BIT4){
        __no_operation(); // Car exits
        space++;
        P1IFG &= ~BIT4;
    }
}


