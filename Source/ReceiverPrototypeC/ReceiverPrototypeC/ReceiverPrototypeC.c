/*
 * ReceiverPrototypeC.c
 *
 * Created: 1/26/2015 12:44:42 PM
 *  Author: joe
 */


//CPU SPEED
#ifndef F_CPU
#define F_CPU 20000000UL //20 MHz
#endif

//SERIAL Macros
#ifndef BAUD
#define BAUD 9600
#endif
//#define UBBR_VALUE ((F_CPU/(USART_BAUDRATE * 16UL))-1)
#ifndef SAMPLECOUNT
#define SAMPLECOUNT 1024
#endif

#ifndef MESSAGELENGTH
#define MESSAGELENGTH 384
#endif

#include <stdio.h>
#include <util/delay.h>
#include <util/setbaud.h>
#include <avr/interrupt.h>
#include <avr/io.h>
//#include "C:\Users\joe\OneDrive - Johns Hopkins University\Atmel Studio\6.1\KBIDCommon\KBIDCommon\KBIDCommon.h"
#include "C:\Users\Joseph\JhuOneDrive\OneDrive for Business\Atmel Studio\6.1\KBIDCommon\KBIDCommon\KBIDCommon.h"

//TODO make this better and remove write ticket to message method
void writeDeviceIDToMessage(char *message, char deviceID[4])
{
	message[1] = deviceID[0];
	message[2] = deviceID[1];
	message[3] = deviceID[2];
	message[4] = deviceID[3];
}

void dumpMessage(char *message)
{
	int len = 0; 
	printf("Message Type: %i\n", message[0]);
	printf("Device ID: %02x:%02x:%02x:%02x\n", message[1],message[2],message[3],message[4]);
	extractLengthFromMessage(message, &len);
	printf("Payload Size: %i\n", len);
	printf ("Payload: ");
	if(len < MESSAGELENGTH)
	{
		
			int i = 0;
			while (i + 7 < MESSAGELENGTH && i < len)
			{
				printf("%02x", message[i+7]);
				i++;
			}
	}
	printf("\n");
}

//TODO make this real
void writeTicketToMessage(char message[MESSAGELENGTH])
{
	int i;
	for(i = 0; i < 256; i++)
	{
		message[7+i] = (i%36) + 48; //0x41; //write 0-Z to the ticket over and over again.
	}
	//message[7+i] = 0x42;
	//i++;
	//message[7+i] = 0x43;
}

int waitForMessage(char message[MESSAGELENGTH])
{
	
	TCNT0 = 0; //clear the 8 bit timer
	TIFR0 &= (1 << TOV0); //clear the overflow flag
	TCCR0B |= 0b00000101; //Start the 8 bit timer with 1024x prescaler
	while(!(PINC & (1 << PINC0))) //Wait for the pin to go high
	{
		if((TIFR0 & (1<<TOV0)))
		{
			TCCR0B &= 0b11111000; //Stop the timer
			return 0;
		}
	}
	TCCR0B &= 0b11111000; //Stop the 8 bit timer
	Reset16BitTimer();
	//readMessageDigital(message, &PINC, PINC0);
	readMessageDigital(message);
	return 1;
}

int validateMessage(char *message)
{
	//Check to see if the message is a valid status
	if(message[0] < 1 || message[0] > 2)
		return 0;
	//if it is an authenticated Status
	if(message[0] == 0x02)
		//and the ticket length is 0
		if(message[5] == 0 && message[6] == 0)
			return 0;
	return 1;
}

void sendGetStatusMessage(char *message, char deviceID[4])
{
	message[0] = 0x01; //Set command ID to 1
	writeDeviceIDToMessage(message, deviceID); //Set device ID to broadcast
	message[5] = 0x00; 
	message[6] = 0x00;//Set Payload size to 0 this is the end of the message
	sendMessage(message); //Send the message
	//sendMessage("This is a long test message that I will attempt to send.");
}

void sendAuthMessage(char *message, char deviceID[4], uint16_t ticketSize)
{
	message[0] = 0x02; //Set command ID to 2
	writeDeviceIDToMessage(message, deviceID);
	message[5] = (ticketSize >> 8) & 0x00FF; //Write the high bits of the size to the string
	message[6] = ticketSize & 0x00FF; //write the low bits of the size
	writeTicketToMessage(message); //Change this
	sendMessage(message);
}

void sendDeauthMessage(char *message, char deviceID[4])
{
	message[0] = 0x03; //Set command ID to 3
	writeDeviceIDToMessage(message, deviceID);
	message[5] = 0x00;
	message[6] = 0x00;//Set Payload size to 0 this is the end of the message
	sendMessage(message);
}

//Stream pointer for writing Serial Data
FILE serialStream = FDEV_SETUP_STREAM(USART0SendByte, NULL, _FDEV_SETUP_WRITE);

int volatile messageTimeoutExpired = 0;

int main(void)
{
	//SET UP PORTS
	DDRC &= ~(1 << PINC0) ; //Pin C0 is input
	DDRC |= (1 << PINC1); //Pin C1 is output
	//PORTC |= (1 << PINC0); // Enable pullup resistors on pinC0
	DDRB |= (1 << PINB1); //Pin B1 is output for LED
	DDRB &= ~(1 << PINB2); //Pin B2 is input for button
	PORTB &= (0 << PINB1); //Clear pin B1
	
	//Variables
	char message[MESSAGELENGTH];
	char broadcastID[4] = {0xFF,0xFF,0xFF,0xFF};
	char deviceID[4] = {0x01,0x01,0x01,0x01};
	
	uint16_t ticketSize = 257;
	uint8_t buttonHasBeenReleased = 1;
	
	//Initialize the timer
	Init16BitTimer();
	//INITIALIZE SERIAL
	InitUSART();
	//Initialize ADC
	InitADC();
	//Redirect SDTOUT to Serial Stream
	stdout = &serialStream;
	_delay_ms(100);
	printf("Device Reset\n");
	
	int testcount = 0;
	
	while(1)
	{
		_delay_ms(10);
		//Detect button press and begin workflow
		if ((PINB & (1<<PINB2)) && buttonHasBeenReleased)
		{
			buttonHasBeenReleased = 0;
			//The button has been pressed.  Send a get status message
			int sendAttempts = 0;
			int receivedMessage = 0;
			//Debug code to loop through 3 message types
			switch(testcount%4)
			{
				case 0:
					sendAuthMessage(message, deviceID, 256);
					testcount++;
					if(testcount > 255)
					testcount = 0;
					break;
				case 1:
					while(sendAttempts < 3 && !receivedMessage) //Send a request status 3 times 
					{
						sendGetStatusMessage(message, broadcastID);
						receivedMessage = waitForMessage(message);
						sendAttempts++;	
					}
					if(sendAttempts >=3 && !receivedMessage)
					{
						printf("No reply from bracelet after %i attempts\n", sendAttempts);
						break;
					}
					printf("Message Received, validating...\n");
					if(validateMessage(message))
						printf("Valid message received\n");
					else
						printf("INVALID message received\n");
					dumpMessage(message);
					testcount++;
					if(testcount > 255)
						testcount = 0;
					break;
				case 2:
					sendDeauthMessage(message, deviceID);
					testcount++;
					if(testcount > 255)
						testcount = 0;
					break;
				case 3:
					while(sendAttempts < 3 && !receivedMessage) //Send a request status 3 times
					{
						sendGetStatusMessage(message, broadcastID);
						receivedMessage = waitForMessage(message);
						sendAttempts++;
					}
					if(sendAttempts >=3 && !receivedMessage)
					{
						printf("No reply from bracelet after %i attempts\n", sendAttempts);
						break;
					}
					printf("Message Received, validating...\n");
					if(validateMessage(message))
					printf("Valid message received\n");
					else
					printf("INVALID message received\n");
					dumpMessage(message);
					testcount++;
					if(testcount > 255)
					testcount = 0;
					break;
				
			}
			/*
			sendGetStatusMessage(message, broadcastID);
			waitForMessage(message);
			printf("Message Received, validating...\n");
			if(validateMessage(message))
				printf("Valid message received\n");
			else
				printf("INVALID message received\n");
			dumpMessage(message);*/
			
		}
		if(!(PINB & (1<<PINB2)))
			buttonHasBeenReleased = 1;
	}
}