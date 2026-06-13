/*
 * lcd_i2c.c
 *
 *  Created on: 11 Jun 2026
 *      Author: User
 */

#include "lcd_i2c.h"

static void i2c_delay(void) {
    __delay_cycles(80);
}

static void i2c_start(void) {
    I2C_PORT_OUT |= SDA_PIN | SCL_PIN;
    i2c_delay();
    I2C_PORT_OUT &= ~SDA_PIN;
    i2c_delay();
    I2C_PORT_OUT &= ~SCL_PIN;
    i2c_delay();
}

static void i2c_stop(void) {
    I2C_PORT_OUT &= ~SDA_PIN;
    i2c_delay();
    I2C_PORT_OUT |= SCL_PIN;
    i2c_delay();
    I2C_PORT_OUT |= SDA_PIN;
    i2c_delay();
}

static void i2c_writeBit(unsigned char bit) {
    if (bit) I2C_PORT_OUT |=  SDA_PIN;
    else     I2C_PORT_OUT &= ~SDA_PIN;
    i2c_delay();
    I2C_PORT_OUT |=  SCL_PIN;
    i2c_delay();
    I2C_PORT_OUT &= ~SCL_PIN;
    i2c_delay();
}

static unsigned char i2c_writeByte(unsigned char data) {
    unsigned char i;
    for (i = 0; i < 8; i++) {
        i2c_writeBit(data & 0x80);
        data <<= 1;
    }
    I2C_PORT_DIR &= ~SDA_PIN;
    i2c_delay();
    I2C_PORT_OUT |= SCL_PIN;
    i2c_delay();
    unsigned char ack = (I2C_PORT_IN & SDA_PIN) ? 1 : 0;
    I2C_PORT_OUT &= ~SCL_PIN;
    I2C_PORT_DIR |=  SDA_PIN;
    i2c_delay();
    return ack;
}

static void pcf8574_write(unsigned char data) {
    i2c_start();
    i2c_writeByte((LCD_I2C_ADDR << 1) | 0);
    i2c_writeByte(data);
    i2c_stop();
}

static void lcd_pulseEnable(unsigned char data) {
    pcf8574_write(data |  0x04);
    __delay_cycles(1600);
    pcf8574_write(data & ~0x04);
    __delay_cycles(800);
}

static void lcd_sendNibble(unsigned char nibble, unsigned char rs) {
    unsigned char data = (nibble << 4) | LCD_BACKLIGHT | (rs ? 0x01 : 0x00);
    pcf8574_write(data);
    lcd_pulseEnable(data);
}

static void lcd_send(unsigned char value, unsigned char rs) {
    lcd_sendNibble(value >> 4,   rs);
    lcd_sendNibble(value & 0x0F, rs);
    __delay_cycles(800);
}

void LCD_init(void) {
    __delay_cycles(640000);
    lcd_sendNibble(0x03, 0); __delay_cycles(80000);
    lcd_sendNibble(0x03, 0); __delay_cycles(2400);
    lcd_sendNibble(0x03, 0); __delay_cycles(2400);
    lcd_sendNibble(0x02, 0); __delay_cycles(2400);
    lcd_send(0x28, 0);
    lcd_send(0x0C, 0);
    lcd_send(0x01, 0); __delay_cycles(32000);
    lcd_send(0x06, 0);
}

void LCD_clear(void) {
    lcd_send(0x01, 0);
    __delay_cycles(32000);
}

void LCD_setCursor(unsigned char col, unsigned char row) {
    unsigned char address = (row == 0) ? 0x00 : 0x40;
    address += col;
    lcd_send(0x80 | address, 0);
}

void LCD_printChar(char c) {
    lcd_send((unsigned char)c, 1);
}

void LCD_print(const char *str) {
    while (*str) LCD_printChar(*str++);
}


