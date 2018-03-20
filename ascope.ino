// Arduino oscilloscope
// by Alexander Mukhin
// Public domain

#include <avr/sleep.h>

unsigned char buf[256];
volatile unsigned char n;
volatile char rdy;
unsigned char prescale=7;

#define sbi(port,bit) (port) |= (1<<(bit))
#define cbi(port,bit) (port) &= ~(1<<(bit))

#if 0
ISR(ADC_vect) {
	buf[n] = ADCH;
	++n;
	if (!n) {
		cbi(ADCSRA,ADEN); // stop ADC
		rdy = 1; // set ready flag
		// debug
		PORTB&=B11011111;
	}
}
#endif

ISR(ANALOG_COMP_vect) {
	n = 0; // set counter to buffer head
	cbi(ACSR,ACIE); // disable analog comparator interrupt
	sbi(ADCSRA,ADIE); // enable ADC interrupt
	// debug
	PORTB|=B00100000;
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
	// debug: turn off digital pin 13
	pinMode(13,OUTPUT);
	PORTB&=B11011111;
}

void
loop () {
	// clear flag
	rdy = 0;
	// set ADC clock prescale value
	ADCSRA &= B11111000;
	ADCSRA += B00000111&prescale;
	// disable ADC interrupt
	cbi(ADCSRA,ADIE);
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
	n = 0;
	do {
		unsigned char c=buf[n];
		if (c==0)
			c = 1;
    		Serial.write(c);
		++n;
	} while (n);
	// read the new prescale value, if any
	if (Serial.available())
		prescale = Serial.read();
}
