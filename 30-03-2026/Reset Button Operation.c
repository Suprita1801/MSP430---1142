#include <msp430.h> 
int main(void)
void resetToDefault(void); //Forward declaration
{
    char ct;
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer
	P1DIR |= (BIT0 + BIT6);
	P1OUT &= ~(BIT0 + BIT6);
	P1DIR &= ~BIT4;
	P1REN |= BIT4;
	P1OUT &= ~BIT4;
	
	while((P1IN & BIT4) == BIT4){
	    __delay_cycles(1000000); // 1 sec
	    ct++; // Count of button pressed
	    if(ct >= 3)
	        resetToDefault(); // Go back to this function when the count is >= 3 sec
	}
	while(1){
	    __delay_cycles(100000);
	    P1OUT ^= BIT6; // This condition satisfied when button is not pressed or button released on or before the condition
	}

}

void resetToDefault(void){
    char i;
    for(i=0;i<6;i++){
        __delay_cycles(100000); //
        P1OUT ^= BIT0;
    }
}
