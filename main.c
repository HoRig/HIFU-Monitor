#include <p30F4012.h>
#include <math.h>
#include <dsp.h>
#include "float.h"
#include "frct2flt.c"
#include "main.h"

_FOSC(XT_PLL16 & PRI & CSW_FSCM_OFF); // External primary oscillator with 16x PLL. Gives 29.5 MIPS. Clock Switching disabled.
_FWDT(WDT_OFF);                       // Watchdog timer turned off
_FBORPOR(PWRT_OFF & PBOR_OFF & MCLR_EN); // POR timer off, No brownout reset, Master clear enabled
_FGS(GWRP_OFF & CODE_PROT_OFF); // General code segment write protect off, general segment code protection off

#define BLOCK_LENGTH	128
#define	START			1
#define STOP			0

void SPI_Init(void);
void Controller_Init(void);
void UART_Init(void);
void ProcessUart(void);
void Timer1_Init(void);
long SignalRMS(volatile long*);
long AverageRMS(volatile long*, volatile int*);

volatile long upperThreshold = 0;
volatile long recLong = 0;
volatile long spiIn[128];
volatile long holdPows[256]; // array holding the values of vecPow for a .5 second sonication
volatile long cycleRmsAverage = 0;
volatile int  holdIndex = 0;
volatile char testspi[128];
int forCount;
volatile int spiIndex = 0;
volatile int uartIndex = 0;
volatile int uartFlag = 0;
volatile int startFlag = 0; // flag to determine when data should be sampled
volatile char readyFlag = 0; // flag to determine when the MCU is ready to start normal operation
volatile char rmsToSend; // specifies which byte of the 32-bit vecPow to send
//volatile char multiplyFactor; // specifies what multiplying factor needs to be sent with
						// rmsToSend (0,8,16, or 24)
volatile char rmsUpper; // upper byte of vecPow
volatile char rmsUpMid; // upper middle byte of vecPow
volatile char rmsLowMid; // lower middle byte of vecPow
volatile char rmsLow; // lower byte of vecPow
volatile long TimerCount = 0;
int rmsFlag = 0;
const int absMag = 128;
volatile long vecPow;
volatile long signalMean = 0;
volatile long signalTemp = 0;
volatile long intermTemp = 0; 
volatile long sumTemp = 0;
volatile int tempindex = 0;
volatile char recCom;
volatile char allClear = 1;
volatile char allOn = 0;
volatile char receiveCount = 0; // when doing initial handshaking with computer,
								// keeps track of the number of bytes received
volatile char receivingThreshold = 0; // flag to keep track of when the threshold
									// voltage is being received from computer

long upperRmsBound = 0;
int cycleCount = 0; // counts the number of cycles that have passed without
					// the RMS threshold being reached
char raiseVoltage = 'r';   // command to raise the voltage by 10 mVpp
char lowerVoltage = 'l';   // command to lower the voltage by 10 mVpp
char emergencyLower = 'e'; // emergency command to drop the voltage by 100 mVpp
						   // since a critical threshold has been reached
char voltageOn = 'n';      // command to turn the function generator on
char voltageOff = 'a';	   // command to turn the function generator off
signed char sendCommand = -1;
signed char sendData = -5;

volatile long delayCount = 0;
volatile char waitForVoltageConfirm = 0; // flag for confirming when the function
										 // generator has been turned on or off

void main(void)
{	
	/*
	tempindex = 0b0111111111000000;
	for(forCount = 0; forCount < 128; forCount++)
	{
		spiIn[forCount] = tempindex >> 6;
		rmsFlag = 1;
	}
	//IEC0bits.SPI1IE = 0; // SPI1 interrupts disabled
	//*/
	SPI_Init();
	Controller_Init();
	UART_Init();
	Timer1_Init();
	
	while(readyFlag == 0){}; // wait for first command from computer before starting
	T1CONbits.TON = 1;

	while(1)
	{
		while(waitForVoltageConfirm == 1){}; // waiting for confirmation that function 
											 // generator was turned on/off
		if(startFlag == STOP)
		{
			if(allClear == 0)
			{
				IEC0bits.SPI1IE = 0; // SPI1 interrupts enabled
				IEC0bits.U1RXIE = 0; // U1 Receive interrupts enabled
				IEC0bits.U1TXIE = 0;
				allClear = 1;
				allOn = 0;
				//while(U1STAbits.UTXBF){}
				//U1TXREG = sendCommand; // let the computer know the next byte is a command
				//while(U1STAbits.UTXBF){}
				//U1TXREG = voltageOff; // turn function generator off				
				cycleCount++;
				if(cycleCount > 10)
				{
					while(U1STAbits.UTXBF){}
					U1TXREG = sendCommand; // let the computer know the next byte is a command
					while(U1STAbits.UTXBF){} // more than 10 cycles have passed without a threshold
					U1TXREG = raiseVoltage;  // being reached. Increase voltage
					cycleCount = 0;
				}
				cycleRmsAverage = AverageRMS(&holdPows[0], &holdIndex);
				holdIndex = 0;
				rmsUpper = cycleRmsAverage >> 24;
				rmsUpMid = cycleRmsAverage >> 16;
				rmsLowMid = cycleRmsAverage >> 8;
				rmsLow = cycleRmsAverage;

				//LATBbits.LATB3 = STOP;

				while(U1STAbits.UTXBF){}
				U1TXREG = sendData; // let the computer know the next four bytes are data
				while(U1STAbits.UTXBF){}
				U1TXREG = 1;//rmsLow;
				while(U1STAbits.UTXBF){}
				U1TXREG = 2;//rmsLowMid;
				while(U1STAbits.UTXBF){}			
				U1TXREG = 3;//rmsUpMid;
				while(U1STAbits.UTXBF){}			
				U1TXREG = 4;//rmsUpper;
				IEC0bits.SPI1IE = 1; // SPI1 interrupts enabled
				IEC0bits.U1RXIE = 1; // U1 Receive interrupts enabled
				IEC0bits.U1TXIE = 1;
			}
			//LATBbits.LATB3 = 0;			
		}

		else if(rmsFlag == 1 && startFlag == START) // if the RMS needs calculated and the transducer is on
		{
			if(allOn == 0)
			{
				allClear = 0;
				allOn = 1;
				//while(U1STAbits.UTXBF){}
				//U1TXREG = sendCommand; // let the computer know the next byte is a command
				//while(U1STAbits.UTXBF){}
				//U1TXREG = voltageOn; // turn function generator on
				//for(delayCount = 0; delayCount < 20; delayCount++){} // create a pause between 
																	// sending command to start function generator
																	// and starting sampling
				//LATBbits.LATB3 = START;
			}

			//LATBbits.LATB0 = 1;
			rmsFlag = 0;

			vecPow = SignalRMS(&spiIn[0]);

			if(vecPow >= upperThreshold)
			{
				IEC0bits.SPI1IE = 0; // SPI1 interrupts enabled
				IEC0bits.U1RXIE = 0; // U1 Receive interrupts enabled
				IEC0bits.U1TXIE = 0;
				while(U1STAbits.UTXBF){}
				U1TXREG = sendCommand; // let the computer know the next byte is a command
				while(U1STAbits.UTXBF){}
				U1TXREG = lowerVoltage; // turn function generator off	
				IEC0bits.SPI1IE = 1; // SPI1 interrupts enabled
				IEC0bits.U1RXIE = 1; // U1 Receive interrupts enabled
				IEC0bits.U1TXIE = 1;	
			}						

			rmsUpper = vecPow >> 24;
			rmsUpMid = vecPow >> 16;
			rmsLowMid = vecPow >> 8;
			rmsLow = vecPow;

			if(holdIndex < 256)
			{
				holdPows[holdIndex] = vecPow;
				holdIndex++;
			}

			//LATBbits.LATB0 = 0;
		}
			
	}
	return;
}

void __attribute__((__interrupt__,no_auto_psv)) _T1Interrupt(void)
{	
	// gives half second on, half second off
	IFS0bits.T1IF = 0; // clear timer1 interrupt flag
	TimerCount++;
	if(TimerCount >= 28)
	{
		TimerCount = 0;
		if(startFlag == STOP)
		{
			startFlag = START; 
			while(U1STAbits.UTXBF){}
			U1TXREG = sendCommand; // let the computer know the next byte is a command
			while(U1STAbits.UTXBF){}
			U1TXREG = voltageOn; // turn function generator on			
			//while(!U1STAbits.URXDA){} // wait for confirmation that function generator was turned on
			//IFS0bits.U1RXIF = 0; // clear the U1RX interrupt flag
			//waitForVoltageConfirm = U1RXREG;
			//LATBbits.LATB3 = START;
			waitForVoltageConfirm == 1;
			allOn = 0;
		}
		else
		{
			startFlag = STOP; 
			while(U1STAbits.UTXBF){}
			U1TXREG = sendCommand; // let the computer know the next byte is a command
			while(U1STAbits.UTXBF){}
			U1TXREG = voltageOff; // turn function generator off	
			//while(!U1STAbits.URXDA){} // wait for confirmation that function generator was turned off
			//IFS0bits.U1RXIF = 0; // clear the U1RX interrupt flag
			//waitForVoltageConfirm = U1RXREG;
			//LATBbits.LATB3 = STOP;
			waitForVoltageConfirm == 1;
			allClear = 0;
		}
	}
	return;
}

void __attribute__((__interrupt__,no_auto_psv)) _U1RXInterrupt(void)
{
	LATBbits.LATB1 = 1;
	IEC0bits.SPI1IE = 0;
	IFS0bits.U1RXIF = 0; // clear the U1RX interrupt flag
	recCom = U1RXREG;
	if(U1STAbits.RIDLE)
	{
		U1STAbits.OERR = 0;
		LATBbits.LATB5 = 1;
	}
	else
	{
		LATBbits.LATB5 = 0;
	}

	if(receivingThreshold == 1)
	{
		recLong = 0;
		recLong = recCom;
		if(receiveCount == 0)
		{
			recLong = recLong << 24;
			upperThreshold += (recLong & 0xFF000000);
			while(U1STAbits.UTXBF){}
			U1TXREG = 'k';
		}
		else if(receiveCount == 1)
		{
			recLong = recLong << 16;
			upperThreshold += (recLong & 0x00FF0000);
			while(U1STAbits.UTXBF){}
			U1TXREG = 'k';
		}
		else if(receiveCount == 2)
		{
			recLong = recLong << 8;
			upperThreshold += (recLong & 0x0000FF00);
			while(U1STAbits.UTXBF){}
			U1TXREG = 'k';
		}
		else
		{
			//upperThreshold = upperThreshold << 8;
			upperThreshold += (recLong & 0x000000FF);
			while(U1STAbits.UTXBF){}
			U1TXREG = 'k';
			receivingThreshold = 0;
			receiveCount = 0;
		}
		receiveCount++;
	}
	else
	{
		switch(recCom)
		{
			case 's': // received command to stop sampling
				startFlag = 0;
				//LATBbits.LATB3 = 0;
				asm("nop");
				asm("nop");
				LATBbits.LATB4 = 0;
				asm("nop");
				asm("nop");
				break;
			case 'g': // received command to start sampling ("go")
				readyFlag = 1;
				//LATBbits.LATB3 = 1;
				asm("nop");
				asm("nop");
				LATBbits.LATB4 = 0;
				asm("nop");
				asm("nop");
				break;
			case 'c':
				receivingThreshold = 1;
				receiveCount = 0;
				while(U1STAbits.UTXBF){}
				U1TXREG = 'c';
				break;
			case 'q':
				LATBbits.LATB3 = STOP;
				waitForVoltageConfirm = 0;
				break;
			case 'p':
				LATBbits.LATB3 = START;
				waitForVoltageConfirm = 0;
				//LATBbits.LATB0 = 1;
				break;
			case 'r':
				LATBbits.LATB0 = 1;
			default:  // unknown command received
				LATBbits.LATB4 = 1;
				asm("nop");
				asm("nop");
				break;
		}
	}
	IEC0bits.SPI1IE = 1;

	if(U1STAbits.RIDLE)
	{
		U1STAbits.OERR = 0;
		LATBbits.LATB5 = 1;
	}
	else
	{
		LATBbits.LATB5 = 0;
	}
	LATBbits.LATB1 = 0;

	IFS0bits.U1RXIF = 0; // clear the U1RX interrupt flag
	
	return;
}

void __attribute__((__interrupt__,no_auto_psv)) _SPI1Interrupt(void)
{
	IFS0bits.SPI1IF = 0; // clear the SPI interrupt flag
	IFS0bits.U1RXIF = 0; 
	tempindex = SPI1BUF;
	spiIn[spiIndex] = tempindex >> 6; //SPI1BUF >> 6;		
	spiIndex++;
	if(spiIndex >= 128)
	{
	    spiIndex = 0;
		uartFlag = 1;
		rmsFlag = 1;
	}

	return;
}

long SignalRMS(volatile long *signalArray)
{
	int i = 0;
	signalMean = 0;
	sumTemp = 0;
	for(i = 0; i < BLOCK_LENGTH; i++)
	{
		signalTemp = signalArray[i];
		intermTemp = signalTemp*signalTemp;
		sumTemp = sumTemp + intermTemp;
	}

	signalMean = sumTemp >> 7; // dividing by 128, same as right-shifting 7 bit places
	
	return signalMean;
}

long AverageRMS(volatile long *vecArray, volatile int *upperBound)
{
	int i = 0;
	cycleRmsAverage = 0;
	for(i = 0; i < *upperBound; i++)
	{
		cycleRmsAverage = cycleRmsAverage + vecArray[i];
	}

	cycleRmsAverage = cycleRmsAverage/(*upperBound);

	return cycleRmsAverage;
}
