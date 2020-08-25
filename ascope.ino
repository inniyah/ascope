// Arduino oscilloscope
// Copyright (c) 2020 Alexander Mukhin
// MIT License

// global variables
unsigned char buf[MAXCHS][N]; // data buffer
struct ctl cs; // control structure

// volatile variables
volatile int n; // current sample number
volatile unsigned char ch; // current channel
volatile unsigned char rdy; // ready flag

// set and clear bit macros
#define sbi(port,bit) (port)|=(1<<(bit))
#define cbi(port,bit) (port)&=~(1<<(bit))

// AC ISR
ISR(ANALOG_COMP_vect) {
	if (cs.samp==1) {
		// equivalent-time sampling
		// start timer
		TCCR1B|=cs.prescale;
		// disable AC interrupts
		cbi(ACSR,ACIE);
		// turn on acquisition LED
		PORTB|=B00100000;
	} else {
		// real-time sampling
		register int n;
		register unsigned char *bufptr;
		// turn on acquisition LED
		PORTB|=B00100000;
		// wait for the ongoing conversion and discard its result
		// as it might be below trigger level
		while (!(ADCSRA&(1<<ADIF)));
		ADCSRA|=1<<ADIF;
		// start acquisition
		bufptr=buf[ch];
		for (n=0; n<N; ++n) {
			// wait for the conversion result
			while (!(ADCSRA&(1<<ADIF)));
			// save conversion result
			*bufptr++=ADCH;
			// turn off ADIF flag
			ADCSRA|=1<<ADIF;
		}
		// try next channel
		++ch;
		// any channels left?
		if (ch<cs.chs) {
			// select next channel
			ADMUX=(ADMUX&B11110000)+(ch&B00001111);
			// clear AC interrupt flag
			// as it might be raised while this ISR is running,
			// (we need to keep the phases synchronized)
			ACSR|=1<<ACI;
		} else {
			// done with the last channel
			// disable analog comparator interrupt
			cbi(ACSR,ACIE);
			// turn off acquisition LED
			PORTB&=B11011111;
			// raise ready flag
			rdy=1;
		}
	}
}

// ADC ISR (used in ET mode only)
// we execute it with interrupts always enabled (ISR_NOBLOCK),
// so that the next AC interrupt will be processed
// as soon as it happens, and restoring flags in this ISR's epilogue
// will not delay it
ISR(ADC_vect,ISR_NOBLOCK) {
	// stop timer
	TCCR1B&=B11111000;
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
			ADMUX=(ADMUX&B11110000)+(ch&B00001111);
		} else {
			// done with the last channel
			// turn off acquisition LED
			PORTB&=B11011111;
			// raise ready flag
			rdy=1;
			// we're done
			return;
		}
	}
	// enable AC interrupts and clear AC interrupt flag as well
	// (we don't want to process a stale pending interrupt)
	ACSR|=B00011000;
}

// set selected sampling mode
void
set_mode (struct ctl *cs) {
	if (cs->samp==1) {
		// equivalent-time sampling
		// reset some control structure fields
		cs->trig=1; // normal triggering
		cs->prescale=1; // fastest sampling rate
		// set ADC clock prescale value
		ADCSRA=(ADCSRA&B11111000)+2;
		// set auto-trigger on Timer/Counter1 Compare Match B
		sbi(ADCSRB,ADTS2);
		sbi(ADCSRB,ADTS0);
		// enable ADC interrupt
		sbi(ADCSRA,ADIE);
	} else {
		// real-time sampling
		// reset some control structure fields
		cs->prescale=2; // fastest sampling rate
		// put ADC in free-running mode
		cbi(ADCSRB,ADTS2);
		cbi(ADCSRB,ADTS0);
		// disable ADC interrupt
		cbi(ADCSRA,ADIE);
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
	// disable digital input buffer on all ADC pins (ADC0-ADC7)
	DIDR0=B11111111;
	// enable auto trigger mode
	sbi(ADCSRA,ADATE);
	// enable ADC
	sbi(ADCSRA,ADEN);
	// init AC
	// this trigger mode selection bit is always set
	sbi(ACSR,ACIS1);
	// disable digital input buffer on AIN0 and AIN1
	sbi(DIDR1,AIN1D);
	sbi(DIDR1,AIN0D);
	// init Timer/Counter1
	// reset control registers to the default values
	TCCR1A=0;
	TCCR1B=0;
	TCCR1C=0;
	// stop Timer/Counter0, since we're not using it,
	// and don't want its ISR to delay our AC ISR
	TCCR0B&=B11111000;
	// init serial
	Serial.begin(9600);
	// init LED
	// we use LED 13 as an acquisition indicator
	pinMode(13,OUTPUT);
	PORTB&=B11011111;
	// set initial mode
	cs.samp=0; // RT sampling
	cs.trig=0; // auto-trigger
	cs.chs=1; // one channel
	cs.slope=1; // default
	set_mode(&cs);
#if 0
// enable calibration PWM output
// clear TC2 control registers
TCCR2A=0;
TCCR2B=0;
TIMSK2=0;
// toggle OC2A on compare
sbi(TCCR2A,COM2A0);
// enable CTC (clear counter on compare) mode
sbi(TCCR2A,WGM21);
// use PB3 as output
pinMode(11,OUTPUT);
// set output compare register
//OCR2A=255; // 31.25 kHz at full clk_io
//OCR2A=127; // 62.5 kHz
//OCR2A=63; // 125 kHz
//OCR2A=31; // 250 kHz
//OCR2A=23; // 333 kHz
//OCR2A=15; // 500 kHz
// set clock speed
//sbi(TCCR2B,CS20); // full clk_io
//sbi(TCCR2B,CS21); // clk_io/8
//sbi(TCCR2B,CS22); // clk_io/64
#endif
}

// sweep start-up specific to the selected sampling mode
void
sweep_start (struct ctl cs) {
	if (cs.samp==1) {
		// equivalent-time sampling
		// set initial position and delay
		n=0;
		OCR1B=1;
		// enable AC interrupt
		sbi(ACSR,ACIE);
	} else {
		// real-time sampling
		// set ADC clock prescale value
		ADCSRA=(ADCSRA&B11111000)+(B00000111&cs.prescale);
		// start first conversion
		sbi(ADCSRA,ADSC);
		// deal with trigger modes
		if (cs.trig)
			// normal triggering
			// enable AC interrupt
			sbi(ACSR,ACIE);
		else
			// auto trigger
			// call AC ISR manually
			while (ch<cs.chs)
				ANALOG_COMP_vect();
	}
}

void
loop () {
	unsigned char c;
	// clear ready flag
	rdy=0;
	// set trigger slope
	if (cs.slope)
		// trigger on rising edge
		sbi(ACSR,ACIS0);
	else
		// trigger on falling edge
		cbi(ACSR,ACIS0);
	// select channel 0
	ch=0;
	ADMUX&=B11110000;
	// mode-specific startup actions
	sweep_start(cs);
	// wait for the data to be ready
	do {
		// read the new control word, if available
		if (Serial.available()) {
			struct ctl newcs;
			// stop current sweep
			cbi(ACSR,ACIE); // disable AC interrupt
			TCCR1B&=B11111000; // stop timer
			// read and parse the new control word
			c=Serial.read();
			parsecw(c,&newcs);
			// change mode, if necessary
			if (newcs.samp!=cs.samp)
				set_mode(&newcs);
			cs=newcs;
			// clear ready flag and start new sweep
			rdy=0;
			break;
		}
	} while (!rdy);
	// write out data
	Serial.write(0); // sync marker
	Serial.write(makecw(cs)); // current control word
	if (rdy) {
		Serial.write(1); // data ready flag
		for (ch=0; ch<cs.chs; ++ch)
			for (n=0; n<N; ++n) {
				c=buf[ch][n];
				// we write 1 instead of 0
				// to avoid confusion with the sync marker
				if (c==0) c=1;
				Serial.write(c);
			}
	} else
		Serial.write(255); // data not ready flag
	// wait for the transmission to complete
	Serial.flush();
}
