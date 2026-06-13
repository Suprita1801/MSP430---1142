#ifndef LCD_I2C_H
#define LCD_I2C_H

#include <msp430.h>

#define LCD_I2C_ADDR    0x27
#define LCD_BACKLIGHT   0x08

/* Software I2C pins — P2.6 = SDA, P2.7 = SCL */
#define SDA_PIN         BIT6
#define SCL_PIN         BIT7
#define I2C_PORT_OUT    P2OUT
#define I2C_PORT_DIR    P2DIR
#define I2C_PORT_IN     P2IN

void LCD_init(void);
void LCD_clear(void);
void LCD_setCursor(unsigned char col, unsigned char row);
void LCD_print(const char *str);
void LCD_printChar(char c);

#endif
