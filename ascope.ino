// Arduino oscilloscope.
// Written by Alexander Mukhin.
// Public domain.

#define N 256 // samples in buffer
#define MAXCHS 2 // maximum number of channels

unsigned char buf[MAXCHS][N]; // data buffer
unsigned char chs=1; // current number of channels
unsigned char dt=1; // time difference between samples
unsigned char slope=1; // trigger on rising (1) or falling (0) edge

volatile int n; // current sample number
volatile unsigned char ch; // current channel
volatile unsigned char rdy; // ready flag

#define sbi(port,bit) (port) |= (1<<(bit))
#define cbi(port,bit) (port) &= ~(1<<(bit))

ISR(ANALOG_COMP_vect) {
	// start timer
	sbi(TCCR1B,CS10);
	// disable AC interrupts
	cbi(ACSR,ACIE);
	// turn on acquisition LED
	PORTB |= B00100000;
}

ISR(ADC_vect) {
	// stop timer
	cbi(TCCR1B,CS10);
	// reset counter
	TCNT1 = 0;
	// clear TC1 output compare B match flag
	sbi(TIFR1,OCF1B);
	// save conversion result 
	buf[ch][n] = ADCH;
	// advance position
	++n;
	// increase delay
	OCR1B += dt;
	// is buffer filled?
	if (n==N) {
		// reset position and delay
		n = 0;
		OCR1B = dt;
		// consider the next channel
		++ch;
		// any channels left?
		if (ch<chs) {
			// select the next channel
			ADMUX = (ADMUX&B11110000)+(ch&B00001111);
		} else {
//cbi(ACSR,ACIE);
			// done with the last channel
			// turn off acquisition LED
			PORTB &= B11011111;
			// raise ready flag
			rdy = 1;
			// we're done
			return;
		}
	}
#if 0
	// clear AC interrupt flag
	// (we don't want to process a stale pending interrupt)
	sbi(ACSR,ACI);
	// enable AC interrupts
	sbi(ACSR,ACIE);
#else
unsigned char tmp=ACSR;
//tmp|=(1<<ACI);
tmp|=(1<<ACIE);
ACSR=tmp;
#endif
	// enable interrupts,
	// so we can process the next AC interrupt immediately,
	// before epilogue ends
	sei();
}

void
setup () {
	// Init ADC
	// select AVcc as voltage reference
	cbi(ADMUX,REFS1);
	sbi(ADMUX,REFS0);
	// left-adjust output
	sbi(ADMUX,ADLAR);
	// set ADC clock prescale value to 16 (this gives full 8-bit resolution)
/*
	sbi(ADCSRA,ADPS2);
	cbi(ADCSRA,ADPS1);
	cbi(ADCSRA,ADPS0);
*/
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
	// Init analog comparator
	// this trigger mode selection bit is always set
	sbi(ACSR,ACIS1);
	// disable digital input buffer on AIN0 and AIN1
	sbi(DIDR1,AIN1D);
	sbi(DIDR1,AIN0D);
	// Stop Timer/Counter0, since we're not using it
	// (disabling unused interrupts
	// helps keep the phases of the channels as close as possible)
	TCCR0B &= B11111000;
	// Init Timer/Counter1
	// reset control registers to the default values
	TCCR1A = 0;
	TCCR1B = 0;
	TCCR1C = 0;
	// Init serial
	Serial.begin(9600);
	// Init LED
	// we use LED 13 as an aquisition indicator
	pinMode(13,OUTPUT);
	PORTB&=B11011111;
#if 1
// enable calibration PWM output
// clear TC2 control registers
TCCR2A = 0;
TCCR2B = 0;
TIMSK2 = 0;
// set output compare register
//OCR2A = 63; // 125 kHz
OCR2A = 31; // 250 kHz
//OCR2A = 23; // 333 kHz
//OCR2A = 26;
//OCR2A = 41;
//OCR2A = 15; // 500 kHz
// toggle OC2A on compare
sbi(TCCR2A,COM2A0);
// enable CTC (clear counter on compare) mode
sbi(TCCR2A,WGM21);
// use PB3 as output
pinMode(11,OUTPUT);
// start timer at full clock speed
sbi(TCCR2B,CS20);
#endif
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
	OCR1B = dt;
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
	Serial.write(0); // sync
	Serial.write(chs); // report current number of channels
	Serial.write(dt); // report current time step
	Serial.write(slope); // report current trigger slope
	for (ch=0; ch<chs; ++ch)
		for (n=0; n<N; ++n) {
			c = buf[ch][n];
			if (c==0)
				c = 1;
	    		Serial.write(c);
		}
	// wait for transmit to complete
	Serial.flush();
	// read new settings, if any
	if (Serial.available()) {
		chs = Serial.read();
		if (chs==0)
			chs = 1;
		if (chs>MAXCHS)
			chs = MAXCHS;
		dt = Serial.read();
		if (dt==0)
			dt = 1;
		slope = Serial.read()&1;
	}
}
