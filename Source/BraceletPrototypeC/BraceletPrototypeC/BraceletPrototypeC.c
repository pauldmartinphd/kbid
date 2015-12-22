/*
 * BraceletPrototypeC.c
 *
 * Created: 2/4/2015 3:24:24 PM
 *  Author: joe
 */ 

#define F_CPU 20000000UL //20 MHz

#ifndef MESSAGELENGTH
#define MESSAGELENGTH 384
#endif

#include <stdio.h>
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
//#include "C:\Users\joe\OneDrive - Johns Hopkins University\Atmel Studio\6.1\KBIDCommon\KBIDCommon\KBIDCommon.h"
#include "C:\Users\Joseph\JhuOneDrive\OneDrive for Business\Atmel Studio\6.1\KBIDCommon\KBIDCommon\KBIDCommon.h"

//Global Variables SHOULD BE DECLAIRED AS VOLITILE so the interrupt functions can change them.
int volatile litLed;
int volatile authenticated = 0;
char volatile ticket[MESSAGELENGTH - 20];
int volatile ticketSize = 0;

//Set the device ID to 01:01:01:01
uint8_t deviceID_Byte0 = 0x01;
uint8_t deviceID_Byte1 = 0x01;
uint8_t deviceID_Byte2 = 0x01;
uint8_t deviceID_Byte3 = 0x01;

void deauth()
{
	authenticated = 0;
	PORTB &= 0xFC; // Clear UI Bits
	PORTB |= 0x01; // Turn on Red Light
	
	while (PIND & (1 << PIND2)); //Wait for the wrist band to close
}

void auth()
{
	authenticated = 1;
	PORTB &= 0xFC; // Clear UI Bits
	PORTB |= 0x02; // Turn on Green Light
}

void waitForMessage(char message[MESSAGELENGTH])
{
	//printf("Waiting for message\n");
	while(!(PINC & (1 << PINC0))); //Wait for the pin to go high
	Reset16BitTimer();
	readMessageDigital(message);
	//readMessageDigital(message, &PINC, PINC0, 1, 3);
	return;
}

int validateMessage(char *message)
{
	//Check to see if the message is a valid command
	if(message[0] < 1 || message[0] > 3)
		return 0;
	//Check to see if the message is intended for me
	if(!(message[1] == deviceID_Byte0 && message[2] == deviceID_Byte1 && message[3] == deviceID_Byte2 && message[4] == deviceID_Byte3))
		//And it is not a broadcast
		if(!(message[1] == 0xFF && message[2] == 0xFF && message[3] == 0xFF && message[4] == 0xFF))
			return 0;
	//if it is a get status or deauth
	if(message[0] == 0x01 || message[0] == 0x03)
		//and the ticket length is GT 0
		if(message[5] > 0 || message[6] > 0)
			return 0;
	return 1;		
}

void handleAuthMessage(char *message, char *ticket, int *ticketLength)
{
	extractLengthFromMessage(message, ticketLength);
	if(*ticketLength > MESSAGELENGTH - 20)
		return; //If the ticket is too big, do nothing
	byteByByteCopy(ticket, message, 0, 6, *ticketLength);
	auth();
}

void handleDeauthMessage(char* ticket, int *ticketLength)
{
	int i;
	for(i=0; i < *ticketLength; i++)
	{
		//wipe the memory
		ticket[i] = 0xff; //11111111
		ticket[i] = 0x55; //01010101
		ticket[i] = 0xAA; //10101010
		ticket[i] = 0x00; //00000000
	}
	*ticketLength = 0;
	deauth();
}

void sendStatusMessage(char *message, char *ticket, int *ticketLength)
{
	message[1] = deviceID_Byte0;
	message[2] = deviceID_Byte1;
	message[3] = deviceID_Byte2;
	message[4] = deviceID_Byte3;
	if(authenticated)
	{
		message[0] = 0x02;
		message[5] = (*ticketLength >> 8) & 0x00FF; //Write the high bits of the size to the string
		message[6] = *ticketLength & 0x00FF; //write the low bits of the size
		byteByByteCopy(message, ticket, 7, 0, *ticketLength);
	}
	else
	{
		message[0] = 0x01;
		message[5] = 0x00;
		message[6] = 0x00;
	}
	sendMessage(message);
}

ISR(INT0_vect)
{
	handleDeauthMessage(ticket, &ticketSize);
	deauth();
}

FILE serialStream = FDEV_SETUP_STREAM(USART0SendByte, NULL, _FDEV_SETUP_WRITE);

int main(void)
{
	DDRB = 0xFF; // Port B is output (user Interface)
	DDRC &= ~(1 << PINC0) ; //Pin C0 is input
	DDRC |= (1 << PINC1); //Pin C1 is output
	DDRD = 0x00;  //Port D is input for the interrupt
	
	PORTD |= (1 << PIND2); // Enable the pullup resistor for the band sensor
	//PORTC |= (1 << PINC0); // Enable the pullup resistor for the amplifier output/signal input
	EIMSK |= (1 << INT0); //Enable Interrupt 0
	EICRA |= (1 << ISC00); //Set Int 0 to fire on any logical change
	
	//Variables
	char message[MESSAGELENGTH];
	
	_delay_ms(100);
	
	InitUSART();
	Init16BitTimer();
	sei();  //Enable global interrupts
	
	
	stdout = &serialStream;
	
	deauth();
	printf("Bracelet on, waiting for signal.\n");
	
	//REMOVE
	ticketSize = 256;
	int i;
	for(i = 0; i < ticketSize; i++)
		ticket[i] = 'A';
	auth();
	    
	while(1)
    {
		while(PINC & (1 << PINC0)); //Do nothing until the user touches the sensor
		_delay_ms(1);	//Delay to let signal smooth out
		//printf("Touch Detected\n");
		waitForMessage(message);
		if(validateMessage(message))
		{
			switch(message[0])
			{
				case 0x01:
				{
					_delay_ms(1);
					sendStatusMessage(message, ticket, &ticketSize);
					printf("Send Status\n");
					break;
				}
				case 0x02:
				{
					handleAuthMessage(message, ticket, &ticketSize);
					printf("Authenticate\n");
					break;
				}
				case 0x03:
				{
					handleDeauthMessage(ticket, &ticketSize);
					printf("Deauthenticate\n");
					break;
				}	
			}
			 
			//DEBUG CODE
			/*
			printf("Ticket Size:  %d\n", ticketSize);
			printf("message in main:  \"");
			int i;
			for(i=0; i < 7+ticketSize; i++)
				USART0SendByte(message[i]);
			printf("\"\n");
			
			//print the ticket every second (Comment out the rest of the functionality)
			printf("Ticket Size: %i\n", ticketSize);
			printf("Ticket: %s", ticket);
			printf("\n");
			//_delay_ms(1000);			
			*/
		}
		_delay_ms(100);  //Delay to let signal smooth out
    }
	
}