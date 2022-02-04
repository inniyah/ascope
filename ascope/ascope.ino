// Arduino oscilloscope
// Copyright (c) 2021 Alexander Mukhin
// MIT License

#include <avr/io.h>
#include <avr/interrupt.h>

#include "ascope.h"

// global variables
unsigned char buf[MAXCHS][N]; // data buffer
struct ctl cs; // control structure

// volatile variables
volatile int n; // current sample number
volatile unsigned char ch; // current channel
volatile unsigned char rdy; // ready flag

// test, set and clear bit macros
#define vbit(p,b) ((p)&1<<(b))
#define sbi(p,b) (p)|=1<<(b)
#define cbi(p,b) (p)&=~(1<<(b))
#define sbi2(p,b,c) (p)|=1<<(b)|1<<(c)
#define cbi3(p,b,c,d) (p)&=~(1<<(b)|1<<(c)|1<<(d))

// set channel
void
set_chan (void) {
	// disable ADC (as suggested in the datasheet, section 28.5)
	cbi(ADCSRA,ADEN);
	// set channel
	ADMUX=(ADMUX&0xf0)+(ch&0x0f);
	// enable ADC
	sbi(ADCSRA,ADEN);
}

// transmit a character over the serial
void
tx (unsigned char c) {
	// wait for the transmit buffer to be ready
	while (!vbit(UCSR0A,UDRE0));
	// put data into the buffer
	UDR0=c;
}

// AC ISR
ISR(ANALOG_COMP_vect) {
	if (cs.samp==1) {
		// equivalent-time sampling
		// start timer
		TCCR1B|=cs.prescale;
		// disable AC interrupts
		cbi(ACSR,ACIE);
		// turn on acquisition LED
		sbi(PORTB,PORTB5);
	} else {
		// real-time sampling
		register int n;
		register unsigned char *bufptr;
		// turn on acquisition LED
		sbi(PORTB,PORTB5);
		// wait for the ongoing conversion and discard its result
		// as it might be below trigger level
		while (!vbit(ADCSRA,ADIF));
		sbi(ADCSRA,ADIF);
		// start acquisition
		bufptr=buf[ch];
		for (n=0; n<N; ++n) {
			// wait for the conversion result
			while (!vbit(ADCSRA,ADIF));
			// save conversion result
			*bufptr++=ADCH;
			// turn off ADIF flag
			sbi(ADCSRA,ADIF);
		}
		// try next channel
		++ch;
		// any channels left?
		if (ch<cs.chs) {
			// select next channel
			set_chan();
			// start first conversion
			// (since set_chan() stops free-running)
			sbi(ADCSRA,ADSC);
			// clear AC interrupt flag
			// as it might be raised while this ISR is running,
			// (we need to keep the phases synchronized)
			sbi(ACSR,ACI);
		} else {
			// done with the last channel
			// disable AC interrupt
			cbi(ACSR,ACIE);
			// turn off acquisition LED
			cbi(PORTB,PORTB5);
			// raise ready flag
			rdy=1;
		}
	}
}

// ADC ISR (used in ET mode only)
// we execute it with interrupts always enabled (ISR_NOBLOCK),
// so that the next AC interrupt will be processed as soon as it happens,
// and restoring flags in this ISR's epilogue will not delay it
ISR(ADC_vect,ISR_NOBLOCK) {
	// stop timer
	cbi3(TCCR1B,CS12,CS11,CS10);
	// reset counter
	TCNT1=0;
	// clear TC1 output compare B match flag
	sbi(TIFR1,OCF1B);
	// save conversion result 
	buf[ch][n]=ADCH;
	// advance position
	++n;
	// increase delay
	++OCR1B;
	// is buffer filled?
	if (n==N) {
		// reset position and delay
		n=0;
		OCR1B=1;
		// consider the next channel
		++ch;
		// any channels left?
		if (ch<cs.chs) {
			// select the next channel
			set_chan();
		} else {
			// done with the last channel
			// turn off acquisition LED
			cbi(PORTB,PORTB5);
			// raise ready flag
			rdy=1;
			// we're done
			return;
		}
	}
	// enable AC interrupts and clear AC interrupt flag as well
	// (we don't want to process a stale pending interrupt)
	sbi2(ACSR,ACI,ACIE);
	// from now on, an AC interrupt can happen
}

void
setup (void) {
	// --- init ADC ---
	// select AVcc as voltage reference
	cbi(ADMUX,REFS1);
	sbi(ADMUX,REFS0);
	// left-adjust output
	sbi(ADMUX,ADLAR);
	// disable digital input buffer on all ADC pins (ADC0-ADC7)
	DIDR0=0xff;
	// enable auto trigger mode
	sbi(ADCSRA,ADATE);
	// enable ADC
	sbi(ADCSRA,ADEN);
	// --- init AC ---
	// this trigger mode selection bit is always set
	sbi(ACSR,ACIS1);
	// disable digital input buffer on AIN0 and AIN1
	sbi(DIDR1,AIN1D);
	sbi(DIDR1,AIN0D);
	// --- init serial ---
	// set baud rate to 9600
	UBRR0L=16000000/16/9600-1;
	// frame format is 8N1 by default
	// enable RX and TX
	sbi2(UCSR0B,RXEN0,TXEN0);
	// --- init acquisition LED ---
	sbi(DDRB,DDB5); // output
	cbi(PORTB,PORTB5); // off for now
	// --- set initial mode ---
	cs.samp=0; // RT sampling
	cs.trig=0; // auto-trigger
	cs.chs=1; // one channel
	cs.slope=1; // default
	cs.prescale=2; // fastest rate
}

// sweep start-up actions
void
start_sweep (void) {
	// set trigger slope
	if (cs.slope)
		// trigger on rising edge
		sbi(ACSR,ACIS0);
	else
		// trigger on falling edge
		cbi(ACSR,ACIS0);
	// select channel 0
	ch=0;
	set_chan();
	// mode-specific actions
	if (cs.samp==1) {
		// equivalent-time sampling
		// set ADC clock prescale value
		ADCSRA=(ADCSRA&0xf8)+2; // fastest
		// set auto-trigger on Timer/Counter1 Compare Match B
		sbi(ADCSRB,ADTS2);
		sbi(ADCSRB,ADTS0);
		// enable ADC interrupt
		sbi(ADCSRA,ADIE);
		// set initial position and delay
		n=0;
		OCR1B=1;
		// enable AC interrupt
		sbi(ACSR,ACIE);
	} else {
		// real-time sampling
		// set ADC clock prescale value
		ADCSRA=(ADCSRA&0xf8)+cs.prescale;
		// put ADC in free-running mode
		cbi(ADCSRB,ADTS2);
		cbi(ADCSRB,ADTS0);
		// disable ADC interrupt
		cbi(ADCSRA,ADIE);
		// start first conversion
		sbi(ADCSRA,ADSC);
		// deal with trigger modes
		if (cs.trig)
			// normal triggering
			// enable AC interrupt
			sbi(ACSR,ACIE);
		else
			// auto trigger
			// call AC ISR immediately
			while (ch<cs.chs)
				ANALOG_COMP_vect();
	}
}

void
sweep (void) {
	unsigned char c;
	// clear ready flag
	rdy=0;
	// start sweep
	start_sweep();
	// wait for the data to be ready
	do {
		// read the new control word, if available
		if (vbit(UCSR0A,RXC0)) {
			// stop current sweep
			cbi(ACSR,ACIE); // disable AC interrupt
			cbi3(TCCR1B,CS12,CS11,CS10); // stop timer (for ET mode)
			// read and parse the new control word
			c=UDR0;
			parsecw(c,&cs);
			// clear ready flag and start new sweep
			rdy=0;
			break;
		}
	} while (!rdy);
	// write out data
	tx(0); // sync marker
	tx(makecw(cs)); // current control word
	if (rdy) {
		tx(1); // data ready flag
		for (ch=0; ch<cs.chs; ++ch)
			for (n=0; n<N; ++n) {
				c=buf[ch][n];
				// we write 1 instead of 0
				// to avoid confusion with the sync marker
				if (c==0) c=1;
				tx(c);
			}
	} else
		tx(255); // data not ready flag
}

int
main (void) {
	setup();
	while (1) sweep();
}
