// Arduino oscilloscope
// by Alexander Mukhin
// Public domain

#include <avr/sleep.h>

// samples in buffer
#define N 256

unsigned char buf[N];
unsigned char prescale=7;
volatile unsigned char rdy=0;

#define sbi(port,bit) (port) |= (1<<(bit))
#define cbi(port,bit) (port) &= ~(1<<(bit))

ISR(ANALOG_COMP_vect) {
	register int n;
	register unsigned char *bufptr;
	// disable analog comparator interrupt
	cbi(ACSR,ACIE);
	// turn on acquisition LED
	PORTB|=0b00100000;
	// clear completed ADC result as it might be below trigger level
	ADCSRA |= 1<<ADIF;
	// start acquisition
	bufptr = buf;
	n = N;
	do {
		// wait for the conversion result
		while (!(ADCSRA&(1<<ADIF)));
		// save conversion result
		*bufptr++ = ADCH;
		// turn off ADIF flag
		ADCSRA |= 1<<ADIF;
	} while (--n);
	// turn off acquisition LED
	PORTB&=0b11011111;
	// raise ready flag
	rdy = 1;
}

void
setup () {
	// init ADC
	// select AVcc as voltage reference
	cbi(ADMUX,REFS1);
	sbi(ADMUX,REFS0);
	// left-adjust output
	sbi(ADMUX,ADLAR);
	// use ADC0 as input (selected by default)
	// enable auto-triggering
	sbi(ADCSRA,ADATE);
	// disable digital input buffer on ADC0
	sbi(DIDR0,ADC0D);
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
	PORTB&=0b11011111;
}

void
loop () {
	int n;
	unsigned char c;
	// clear ready flag
	rdy = 0;
	// set ADC clock prescale value
	ADCSRA &= 0b11111000;
	ADCSRA += 0b00000111&prescale;
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
	Serial.write(prescale); // report current prescale
	for (n=0; n<N; ++n) {
		c = buf[n];
		if (c==0)
			c = 1;
    		Serial.write(c);
	}
	// read the new prescale value, if any
	if (Serial.available())
		prescale = Serial.read();
}
