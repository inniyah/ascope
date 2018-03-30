// Arduino oscilloscope.
// Written by Alexander Mukhin.
// Public domain.

#include <avr/sleep.h>

#define N 256 // samples in buffer
#define MAXCHS 2 // maximum number of channels
#define T 3 // number of samples to average

unsigned char buf[MAXCHS][N]; // data buffer
unsigned char dly=0;
unsigned char chs=1; // current number of channels
volatile int n; // current sample number
volatile unsigned char t; // trial number
volatile unsigned int s; // sum of trial values
volatile unsigned char ch; // current channel
volatile unsigned char rdy; // ready flag
unsigned char cw=(chs<<4)+dly; // control word

#define sbi(port,bit) (port) |= (1<<(bit))
#define cbi(port,bit) (port) &= ~(1<<(bit))

ISR(ANALOG_COMP_vect) {
	unsigned char prev; // previous register value
	int d;
	// turn on acquisition LED
	PORTB|=B00100000;
	// wait
	d = n*dly;
	while (d) {
		d--;
		PORTB|=B00100000; // XXX
	}
	// start conversion
	sbi(ADCSRA,ADSC);
	// wait for the conversion to complete
#if 0
	while (!(ADCSRA&(1<<ADIF)));
	// turn off ADIF flag
	ADCSRA |= 1<<ADIF;
#else
	while(ADCSRA&(1<<ADSC));
#endif
	// save result
	s += ADCH;
	++t;
	if (t==T) {
		buf[ch][n] = s/T;
		// clear sum and trial counter
		s = 0;
		t = 0;
		// advance counters
		++n;
		if (n==N) {
			n = 0;
			++ch;
			if (ch<chs) {
				// select next channel
				prev = ADMUX;
				ADMUX = (prev&B11110000)+(ch&B00001111);
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
	}
	// clear AC interrupt flag
	// (as it might be raised while this ISR is running)
	ACSR |= 1<<ACI;
}

void
setup () {
	// init ADC
	// select AVcc as voltage reference
	cbi(ADMUX,REFS1);
	sbi(ADMUX,REFS0);
	// left-adjust output
	sbi(ADMUX,ADLAR);
	// set ADC clock prescale value to 4
	sbi(ADCSRA,ADPS2);
	// disable digital input buffer on all ADC pins (ADC0-ADC7)
	DIDR0 = B11111111;
	// enable ADC
	sbi(ADCSRA,ADEN);
	// start first conversion
	sbi(ADCSRA,ADSC);
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
//	TIMSK0=0;
sbi(PRR,PRTIM0);
}

void
loop () {
	unsigned char c;
	int k;
	// clear ready flag
	rdy = 0;
	// clear sum and trial counter
	s = 0;
	t = 0;
	// select channel 0
	ch = 0;
	ADMUX &= B11110000;
	// set initial position
	n = 0;
	// enable analog comparator interrupt
	sbi(ACSR,ACIE);
	// enable sleep mode
	sbi(SMCR,SE);
	// wait for the data to be ready
	while (!rdy)
//		sleep_cpu();
	// disable sleep mode
	cbi(SMCR,SE);
	// write out data
	Serial.write(0); // sync
	Serial.write(cw); // report current control word
	for (ch=0; ch<chs; ++ch)
		for (k=0; k<N; ++k) {
			c = buf[ch][k];
			if (c==0)
				c = 1;
	    		Serial.write(c);
		}
	// wait for transmit to complete
	Serial.flush();
	// read and parse the new control word, if any
	if (Serial.available()) {
		cw = Serial.read();
		dly = cw&B00001111;
		chs = cw>>4;
	}
}
