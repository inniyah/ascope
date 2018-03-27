// Arduino oscilloscope.
// Written by Alexander Mukhin.
// Public domain.

#include <avr/sleep.h>

#define N 256 // samples in buffer
#define MAXCHS 2 // maximum number of channels

unsigned char buf[MAXCHS][N]; // data buffer
unsigned char prescale=7; // ADC clock prescale value
volatile unsigned char ch=0; // current channel
unsigned char chs=1; // current number of channels
volatile unsigned char rdy=0; // ready flag
unsigned char cw=(chs<<4)+prescale; // control word

#define sbi(port,bit) (port) |= (1<<(bit))
#define cbi(port,bit) (port) &= ~(1<<(bit))

ISR(ANALOG_COMP_vect) {
	register int n;
	register unsigned char *bufptr;
	unsigned char prev; // previous register value
	// turn on acquisition LED
	PORTB|=B00100000;
	// clear completed ADC result as it might be below trigger level
	ADCSRA |= 1<<ADIF;
	// start acquisition
	bufptr = buf[ch];
	for (n=0; n<N; ++n) {
		// wait for the conversion result
		while (!(ADCSRA&(1<<ADIF)));
		// save conversion result
		*bufptr++ = ADCH;
		// turn off ADIF flag
		ADCSRA |= 1<<ADIF;
	}
	++ch;
	if (ch<chs) {
#if 0
		// stop ADC
		cbi(ADCSRA,ADEN);
#endif
		// select next channel
		prev = ADMUX;
		ADMUX = (prev&B11110000)+(ch&B00001111);
#if 0
		// start ADC
		sbi(ADCSRA,ADEN);
		sbi(ADCSRA,ADSC);
#endif
		// clear AC interrupt flag
		// (as it might be raised while this ISR is running)
		ACSR |= 1<<ACI;
	} else {
		// done with the last channel
		// disable analog comparator interrupt
		cbi(ACSR,ACIE);
		// turn off acquisition LED
		PORTB&=B11011111;
		// raise ready flag
		rdy = 1;
	}
}

void
setup () {
	// init ADC
	// select AVcc as voltage reference
	cbi(ADMUX,REFS1);
	sbi(ADMUX,REFS0);
	// left-adjust output
	sbi(ADMUX,ADLAR);
	// enable auto-triggering
	sbi(ADCSRA,ADATE);
	// disable digital input buffer on all ADC pins (ADC0-ADC7)
	DIDR0 = B11111111;
	// init analog comparator
	// select intr on rising edge
	sbi(ACSR,ACIS1);
	sbi(ACSR,ACIS0);
	// disable digital input buffer on AIN0 and AIN1
	sbi(DIDR1,AIN1D);
	sbi(DIDR1,AIN0D);
	// init serial
	Serial.begin(9600);
	// we use LED 13 as an aquisition indicator
	pinMode(13,OUTPUT);
	PORTB&=B11011111;
	// disable Timer/Counter0 interrupts
	// (disabling unused interrupts
	// helps keep the phases of the channels as close as possible)
	TIMSK0=0;
}

void
loop () {
	int n;
	unsigned char c;
	unsigned char prev; // previous register value
	// clear ready flag
	rdy = 0;
	// set ADC clock prescale value
	prev = ADCSRA;
	ADCSRA = (prev&B11111000)+(B00000111&prescale);
	// select channel 0
	ch = 0;
	ADMUX &= B11110000;
	// start ADC
	sbi(ADCSRA,ADEN);
	sbi(ADCSRA,ADSC);
	// enable analog comparator interrupt
	sbi(ACSR,ACIE);
	// enable sleep mode
	sbi(SMCR,SE);
	// wait for the data to be ready
	while (!rdy)
		sleep_cpu();
	// disable sleep mode
	cbi(SMCR,SE);
	// write out data
	Serial.write(0); // sync
	Serial.write(cw); // report current control word
	for (ch=0; ch<chs; ++ch)
		for (n=0; n<N; ++n) {
			c = buf[ch][n];
			if (c==0)
				c = 1;
	    		Serial.write(c);
		}
	// wait for transmit to complete
	Serial.flush();
	// read and parse the new control word, if any
	if (Serial.available()) {
		cw = Serial.read();
		prescale = cw&B00000111;
		chs = cw>>4;
	}
}
