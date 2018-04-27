// Arduino oscilloscope.
// Written by Alexander Mukhin.
// Public domain.

#define N 256 // samples in buffer
#define MAXCHS 2 // maximum number of channels

// global variables
unsigned char buf[MAXCHS][N]; // data buffer
unsigned char prescale=1; // timer clock prescale value
unsigned char slope=1; // trigger on rising (1) or falling (0) edge
unsigned char chs=1; // current number of channels

// make oscilloscope control word
unsigned char
makecw (unsigned char prescale, unsigned char slope, unsigned char chs) {
	return (chs<<4)+(slope<<3)+prescale;
}

// parse oscilloscope control word
void
parsecw (unsigned char cw, \
	 unsigned char *prescale, unsigned char *slope, unsigned char *chs) {
	*prescale = cw&0x7;
	*slope = (cw&0x8)>>3;
	*chs = (cw&0x70)>>4;
}

// volatile variables
volatile int n; // current sample number
volatile unsigned char ch; // current channel
volatile unsigned char rdy; // ready flag

// set and clear bit macros
#define sbi(port,bit) (port) |= (1<<(bit))
#define cbi(port,bit) (port) &= ~(1<<(bit))

// AC ISR
ISR(ANALOG_COMP_vect) {
	// start timer
	TCCR1B |= prescale;
	// disable AC interrupts
	cbi(ACSR,ACIE);
	// turn on acquisition LED
	PORTB |= B00100000;
}

// ADC ISR
// we execute it with interrupts always enabled (ISR_NOBLOCK),
// so that the next AC interrupt will be processed
// as soon as it happens, and restoring flags in this ISR's epilogue
// will not delay it
ISR(ADC_vect,ISR_NOBLOCK) {
	// stop timer
	TCCR1B &= B11111000;
	// reset counter
	TCNT1 = 0;
	// clear TC1 output compare B match flag
	sbi(TIFR1,OCF1B);
	// save conversion result 
	buf[ch][n] = ADCH;
	// advance position
	++n;
	// increase delay
	++OCR1B;
	// is buffer filled?
	if (n==N) {
		// reset position and delay
		n = 0;
		OCR1B = 1;
		// consider the next channel
		++ch;
		// any channels left?
		if (ch<chs) {
			// select the next channel
			ADMUX = (ADMUX&B11110000)+(ch&B00001111);
		} else {
			// done with the last channel
			// turn off acquisition LED
			PORTB &= B11011111;
			// raise ready flag
			rdy = 1;
			// we're done
			return;
		}
	}
	// enable AC interrupts and clear AC interrupt flag as well
	// (we don't want to process a stale pending interrupt)
	ACSR |= B00011000;
}

void
setup () {
	// init ADC
	// select AVcc as voltage reference
	cbi(ADMUX,REFS1);
	sbi(ADMUX,REFS0);
	// left-adjust output
	sbi(ADMUX,ADLAR);
	// set ADC clock prescale value
	ADCSRA = (ADCSRA&B11111000)+2;
	// disable digital input buffer on all ADC pins (ADC0-ADC7)
	DIDR0 = B11111111;
	// set auto-trigger on Timer/Counter1 Compare Match B
	sbi(ADCSRB,ADTS2);
	sbi(ADCSRB,ADTS0);
	// enable auto trigger mode
	sbi(ADCSRA,ADATE);
	// enable ADC interrupt
	sbi(ADCSRA,ADIE);
	// enable ADC
	sbi(ADCSRA,ADEN);
	// init AC
	// this trigger mode selection bit is always set
	sbi(ACSR,ACIS1);
	// disable digital input buffer on AIN0 and AIN1
	sbi(DIDR1,AIN1D);
	sbi(DIDR1,AIN0D);
	// stop Timer/Counter0, since we're not using it,
	// and don't want its ISR to delay our AC ISR
	TCCR0B &= B11111000;
	// init Timer/Counter1
	// reset control registers to the default values
	TCCR1A = 0;
	TCCR1B = 0;
	TCCR1C = 0;
	// init serial
	Serial.begin(9600);
	// init LED
	// we use LED 13 as an aquisition indicator
	pinMode(13,OUTPUT);
	PORTB&=B11011111;
}

void
loop () {
	unsigned char c;
	// clear ready flag
	rdy = 0;
	// select channel 0
	ch = 0;
	ADMUX &= B11110000;
	// set initial position and delay
	n = 0;
	OCR1B = 1;
	// set trigger slope
	if (slope)
		// trigger on rising edge
		sbi(ACSR,ACIS0);
	else
		// trigger on falling edge
		cbi(ACSR,ACIS0);
	// enable analog comparator interrupt
	sbi(ACSR,ACIE);
	// wait for the data to be ready
	while (!rdy);
	// write out data
	Serial.write(0); // sync marker
	Serial.write(makecw(prescale,slope,chs)); // current control word
	for (ch=0; ch<chs; ++ch)
		for (n=0; n<N; ++n) {
			c = buf[ch][n];
			if (c==0)
				c = 1;
	    		Serial.write(c);
		}
	// wait for the transmission to complete
	Serial.flush();
	// read the new control word, if available
	if (Serial.available()) {
		c = Serial.read();
		// parse control word
		parsecw(c,&prescale,&slope,&chs);
	}
}
