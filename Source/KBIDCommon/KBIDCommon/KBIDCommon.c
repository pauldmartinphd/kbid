/*
 * KBIDCommon.c
 *	This file contains all of the functionality that is common between the bracelet and the auth module
 *
 * Created: 3/26/2015 2:08:29 PM
 *  Author: joe
 */ 

//CPU SPEED MACRO
#ifndef F_CPU
#define F_CPU 20000000UL //20 MHz
#endif

//SERIAL Macros
#ifndef BAUD
#define BAUD 9600
#endif
//#define UBBR_VALUE ((F_CPU/(USART_BAUDRATE * 16UL))-1)
#define SAMPLECOUNT 1024

#ifndef DELAYARG
#define DELAYARG 100
#endif

#ifndef MESSAGELENGTH
#define MESSAGELENGTH 384
#endif

#ifndef RECEIVEPINS
#define RECEIVEPINS PINC
#endif

#ifndef RECEIVEPINSHIFT
#define RECEIVEPINSHIFT PINC0
#endif


#include <avr/io.h>
#include <util/delay.h>
#include <util/setbaud.h>
#include <avr/interrupt.h>


void Init16BitTimer()
{
	//Set timer to no prescaler
	TCCR1B |= (1 << CS10);
	//Set timer to x/8 prescaler
	//TCCR1B |= (1 << CS11);
	//Set timer to x/64 prescaler
	//TCCR1B |= (1 << CS11) | (1 << CS10);
	//Set timer prescaller to 256
	//TCCR1B |= (1 << CS12);
	//Set timer prescaller to 1024
	//TCCR1B |= (1 << CS12) | (1 << CS10);
}

unsigned int Read16BitTimer()
{
	unsigned char sreg;
	unsigned int retVal;
	sreg = SREG;
	cli();
	retVal = TCNT1;
	SREG = sreg;
	//sei();
	return retVal;
}

void Reset16BitTimer()
{
	unsigned char sreg;
	sreg = SREG;
	cli();
	TCNT1 = 0;
	SREG = sreg;
	//sei();
}

void InitADC()
{
	//Disable Power Reduction
	//PRR &= (~(1<<PRADC));
	//Select  Vref = AVcc
	ADMUX |= (1<<REFS0);
	//set prescaler to 128 and enable ADC - BLACK ART
	ADCSRA |= (1<<ADPS2)|(1<<ADPS1)|(1<<ADPS0)|(1<<ADEN);
}

int16_t ReadADC(uint8_t ADCPin)
{
	//Leave the high bits of ADMUX unchanged but set the low bits to the pin number
	ADMUX = (ADMUX & 0xF0) | (ADCPin & 0x0F);
	//Set single conversion mode
	ADCSRA |= (1<<ADSC);
	//Wait for conversion to complete
	while(ADCSRA & (1<<ADSC));
	return ADC;
}

void InitUSART()
{
	//Set Baud Rate Registers
	UBRR0H = UBRRH_VALUE>>8;
	UBRR0L = UBRRL_VALUE;
	//8 Databits, no parady, 1 stop bit
	UCSR0C |= (1 << UCSZ00)|(1 << UCSZ01);
	//Enable Send and Receive
	UCSR0B |= (1 << RXEN0)|(1<<TXEN0);
}

int USART0SendByte(char data)
{
	//change \n to \r with recursion
	if(data == '\n')
	{
		USART0SendByte('\r');
	}
	//wait while previous bite is completed
	while(!(UCSR0A&(1<<UDRE0))){}; //wait
	//Write the data
	UDR0 = data;
	return 0;
}

void extractLengthFromMessage(char *message, int *length)
{
	*length = message[5] << 8;
	*length |= message[6];
}

void byteByByteCopy(char* destination, char* source, int destIndex, int sourceIndex, int length)
{
	int i;
	for (i = 0; i< length; i++)
		destination[destIndex + i] = source[sourceIndex + i];
	return;
}

int setThreshhold()
{
	//TODO Make ADC PIN DYNAMIC
	int oneThreshold = 0;
	int sampleCount = 0;
	int workingValue = 0;
	int DistributionArray[32] = {0};
	//Collect Samples
	for(sampleCount = 0; sampleCount< SAMPLECOUNT - 1; sampleCount++)
	{
		workingValue = ReadADC(0);
		workingValue = workingValue - (workingValue%32);
		workingValue = workingValue / 32;
		DistributionArray[workingValue]++;
	}
	//Find Valley
	int firstPeak = 0;
	int secondPeak = 0;
	int i = 0;
	int valleyFound = 0;
	for(i=0; i<32; i++)
	{
		if(valleyFound == 0)
		{
			if(DistributionArray[i] > DistributionArray[firstPeak])
				firstPeak = i;
			if(DistributionArray[i] == 0 && DistributionArray[firstPeak] > 0)
			{
				valleyFound = 1;
				secondPeak = i;
			}
		}
		else
		{
			if(DistributionArray[i] > DistributionArray[secondPeak])
				secondPeak = i;			
		}
	}
	//if we don't find a second peak
	if(DistributionArray[secondPeak] == 0 || secondPeak == 0)
		oneThreshold = 0;
	else
		oneThreshold = ((firstPeak + secondPeak)/2)*32;
	//DEBUG Code
	/*
	for(i = 0; i < 32; i++)
	{
		if(i>0)
			printf(",");
		printf("%u", DistributionArray[i]);		
	}
	printf("\n");
	for(i = 0; i < 32; i++)
	{
		if(i == firstPeak || i == secondPeak)
			printf(" ^");
		else
		{
			if(i == (oneThreshold/32) && oneThreshold > 0)
				printf("^T");
			else
				printf("  ");
		}
		if (DistributionArray[i] > 9)
			printf(" ");
		if (DistributionArray[i] > 99)
			printf(" ");
	}
	printf("\n");
	if(oneThreshold>0)
		printf("Setting threshold to %u.\n", oneThreshold);
	else
		printf("Did not find two peaks\n");
	//End Debug Code
	*/
	return oneThreshold;
}

void readMessageAnalog()
{
	int oneThreshold = setThreshhold();
	if (oneThreshold  == 0)
	{
		printf("Threshold not set successfully\n");
		return;
	}
	if(oneThreshold < 350)
	{
		printf("Trying again\n");
		oneThreshold = setThreshhold();
	}
	int foundBeginning = 0;
	int foundEnd = 0;
	int highLength = 0;
	int lowLength = 0;
	int lastLowLength = 0;
	int shortLowLegnthMOE = 0;
	//Look for the beginning of the message
	//First reset the timer to start counting the current state, even though we don't yet know what it is
	Reset16BitTimer();
	while (foundBeginning == 0)
	{
		if(ReadADC(0) > oneThreshold)
		{
			//Found a High, let the timer run to measure how long it is
			while (ReadADC(0) > oneThreshold);
			highLength = Read16BitTimer();
			//Reset the timer to start counting the low length
			Reset16BitTimer();
			//Store the old low length value
			lastLowLength = lowLength;
			//Then let the timer run to measure how long it is
			while(ReadADC(0) < oneThreshold);
			lowLength = Read16BitTimer();
			//Reset the timer to start counting the high length
			Reset16BitTimer();
			//If this is the second low
			if(shortLowLegnthMOE == 0 && lastLowLength > 0)
			{
				//Set the Margin of Error to 1/2 of the lowest of the last 2 lows
				if(lastLowLength < lowLength)
					shortLowLegnthMOE = lastLowLength >> 1;
				else
					shortLowLegnthMOE = lowLength >> 1;
			}
			//If we have at least 2 lows and this high is shorter than the previous low (with in the MOE) we have found the first bit.
			if(shortLowLegnthMOE > 0 && highLength < lastLowLength - shortLowLegnthMOE)
				foundBeginning = 1;
		}
		else //This should only happen on the first read
		{
			if(lowLength > 0)
				printf("There may be a problem\n");
			//Found a low, Store the old value
			lastLowLength = lowLength;
			//Then let the timer run to measure how long it is
			while(ReadADC(0) < oneThreshold);
			lowLength = Read16BitTimer();
			//Reset the timer to start counting the high length
			Reset16BitTimer();	
		}
	}
	char messageString[256];
	int i = 0;
	int bitshift = 7;
	//Write the first bit to the message
	messageString [i] = '\0';
	//If the high length is long, write a 1
	if(highLength > lowLength + shortLowLegnthMOE)
		messageString [i] = messageString[i] | (1 << bitshift);
	//decrement the shift
	bitshift--;
	//Now continue reading the rest of the message
	for (i=0; i<256; i++)
	{
		while (bitshift >= 0)
		{
			if(ReadADC(0) > oneThreshold)
			{
				//Found a High, let the timer run to measure how long it is
				while (ReadADC(0) > oneThreshold);
				highLength = Read16BitTimer();
				//Reset the timer to start counting the low length
				Reset16BitTimer();
				//Store the old low length value
				lastLowLength = lowLength;
				//Then let the timer run to measure how long it is
				while(ReadADC(0) < oneThreshold);
				lowLength = Read16BitTimer();
				//Reset the timer to start counting the high length
				Reset16BitTimer();
				//If we are at the end of the message
				if(lowLength > lastLowLength + shortLowLegnthMOE)
				{
					foundEnd = 1;
					//then use the previous low to determine the value of the long
					if(highLength > lastLowLength + shortLowLegnthMOE)
						messageString [i] = messageString[i] | (1 << bitshift);
				}
				else
				{
					//If the high length is long, write a 1
					if(highLength > lowLength + shortLowLegnthMOE)
						messageString [i] = messageString[i] | (1 << bitshift);		
				}
				
			}
			else //This should Never Happen
			{
				printf("Now you are hosed\n");
			}
			if(foundEnd == 1)
				bitshift = 0;  //break while loop
			bitshift--;
		}
		//Clear out the next cell in the message
		messageString[i+1] = '\0';
		if(foundEnd == 1)
			i = 256; //Break for loop
		//reset the bitshift for the next byte
		bitshift = 7;
	}
	
	
	
	//Debug Code
	/*
	printf("Threshold set to %d\n", oneThreshold);
	printf("Found the beginning, I think.\n");
	printf("highLength: %u\n", highLength);
	printf("lowLength: %u\n", lowLength);
	printf("lastLowLength: %u\n", lastLowLength);
	printf("shortLowLegnthMOE: %u\n", shortLowLegnthMOE);
	printf("Here's the message: %s\n", messageString);
	printf("\n");
	*/
	//End Debug Code
	return;
}

/*
*	This function must be called the moment a message begins.
*	It expects the counter to have been reset right before it was called and 
*	it expects the counter to be running.
*	It also expects the signal to be high
*/
void readMessageDigital(char message[MESSAGELENGTH])
{
	int highLength = 0;
	int lowLength = 0;
	int lastLowLength = 0;
	int shortLowLegnthMOE = 0;
	int index = 0;
	int bitshift = 7;
	//Capture First Bit
	while (RECEIVEPINS & (1 << RECEIVEPINSHIFT)); //Wait while the pin is high
	highLength = Read16BitTimer();
	Reset16BitTimer();
	while((RECEIVEPINS & (1 << RECEIVEPINSHIFT)) < 1);
	lowLength = Read16BitTimer();
	Reset16BitTimer();
	shortLowLegnthMOE = lowLength >> 1;
	message[index] = '\0';
	//If the high length is long, write a 1
	if(highLength > lowLength + shortLowLegnthMOE)
	{
		message[index] = message[index] | (1 << bitshift);
	}
	bitshift--;
	while (index < MESSAGELENGTH)
	{
		if(index)
			message[index] = '\0'; // Clear out the next character	
		while(bitshift >= 0)
		{
			//The signal should be High, let the timer run to measure how long it is
			while (RECEIVEPINS & (1 << RECEIVEPINSHIFT));
			highLength = Read16BitTimer();
			//Reset the timer to start counting the low length
			Reset16BitTimer();
			//Store the old low length value
			lastLowLength = lowLength;
			//Then let the timer run to measure how long it is
			while(((RECEIVEPINS & (1 << RECEIVEPINSHIFT)) < 1) && (TIFR1 ^ (1 << TOV1)));
			lowLength = Read16BitTimer();
			//Reset the timer to start counting the high length
			Reset16BitTimer();
			//If low is longer than last low, we are at the end
			if(lowLength > (lastLowLength+shortLowLegnthMOE))
			{
				if(highLength > (lastLowLength + shortLowLegnthMOE))
					message[index] = message[index] | (1 << bitshift);
				message[index+1] = '\0';
				bitshift = -1; // break this while loop
				index = MESSAGELENGTH; // break the outer while loop
				
			}
			else //other wise, just write the bit
			{
				//If the high length is long, write a 1
				if(highLength > lowLength + shortLowLegnthMOE)
				message[index] = message[index] | (1 << bitshift);
				bitshift--;	
			}
		}
		index++;
		bitshift = 7; // reset the bit shift
	}
	//Debug code
	//printf("Epsilon = %d\n", shortLowLegnthMOE);
	return;
}

void sendBit(uint8_t bit)
{
	PORTC |= (1 << PINC1); //Turn the pin on
	_delay_us(DELAYARG); //for Delay
	if(bit == 1) _delay_us(DELAYARG); //Twice if a 1
	PORTC &= ~(1<<PINC1);  //Turn the pin off
	_delay_us(DELAYARG); //for Delay
}

void sendMessage(char *messageString)
{
	uint16_t ticketLength = 0;
	extractLengthFromMessage(messageString, &ticketLength);
	int sendLength = 7 + ticketLength;
	if (sendLength > MESSAGELENGTH) 
		sendLength = MESSAGELENGTH; //Don't allow sending of RAM
	int i;
	for (i=0; i < sendLength; i++)
	{
		//if(messageString[i] == '\0')
		//	i = MESSAGELENGTH;
		//else
		{
			uint8_t mask = 0x80;
			while (mask > 0)
			{
				if(messageString[i] & mask)
				sendBit(1);
				else
				sendBit(0);
				mask = mask >> 1;
			}
		}
	}
	//Send end of message signal (long delay followed by a 0)
	_delay_us(DELAYARG * 4);
	sendBit(0);
}

void sendPreamble()
{
	sendMessage("\xaa\xaa");
}