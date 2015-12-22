/*
 * KBIDCommon.h
 *
 * Created: 3/26/2015 2:56:24 PM
 *  Author: joe
 */ 


#ifndef KBIDCOMMON_H_
#define KBIDCOMMON_H_

void Init16BitTimer();
unsigned int Read16BitTimer();
void Reset16BitTimer();
void InitADC();
int16_t ReadADC(uint8_t ADCPin);
void InitUSART();
int USART0SendByte(char data);
int setThreshhold();
int setPulseLengthThreshold(int oneThreshold);
void readMessageAnalog();
//void readMessageDigital(volatile char* ,volatile uint8_t, uint8_t );
void sendBit(uint8_t bit);
void sendPreamble();
void sendMessage(char messageString[256]);


#endif /* KBIDCOMMON_H_ */