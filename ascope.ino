// Arduino oscilloscope.
// Written by Alexander Mukhin.
// Public domain.

#define N 256 // samples in buffer
#define MAXCHS 2 // maximum number of channels
#define T 1 // number of samples to average

unsigned char buf[MAXCHS][N]; // data buffer
unsigned char chs=1; // current number of channels
unsigned char dt=128; // time difference between samples 

volatile int n; // current sample number
volatile unsigned char t; // trial number
volatile unsigned int s; // sum of trial values
volatile unsigned char ch; // current channel
volatile unsigned char rdy; // ready flag

#define sbi(port,bit) (port) |= (1<<(bit))
#define cbi(port,bit) (port) &= ~(1<<(bit))

ISR(ANALOG_COMP_vect) {
	// disable analog comparator interrupts
	// (they may occur while the timer is counting)
	cbi(ACSR,ACIE);
	// turn on acquisition LED
	PORTB |= B00100000;
	// reset counter
	TCNT1 = 0;
	// start timer
	sbi(TCCR1B,CS10);
}

ISR(TIMER1_COMPA_vect) {
	// start conversion
	sbi(ADCSRA,ADSC);
	// stop timer
	cbi(TCCR1B,CS10);
	// wait for the conversion to complete
	while (ADCSRA&(1<<ADSC));
	// add result to the sum
	s += ADCH;
	++t;
	if (t==T) {
		buf[ch][n] = s/T;
		// clear sum and trial counter
		s = 0;
		t = 0;
		// advance position
		++n;
		// increase delay
		OCR1A += dt;
		// is buffer filled?
		if (n==N) {
			// reset position and delay
			n = 0;
			OCR1A = dt;
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
	}
	// clear AC interrupt flag
	// as it might be raised while this ISR is running
	// (this helps keeping correct phase)
	ACSR |= 1<<ACI;
	// enable analog comparator interrupts
	sbi(ACSR,ACIE);
}

void
setup () {
	// Init ADC
	// select AVcc as voltage reference
	cbi(ADMUX,REFS1);
	sbi(ADMUX,REFS0);
	// left-adjust output
	sbi(ADMUX,ADLAR);
	// set ADC clock prescale value to 4 (this gives full 8-bit resolution)
	sbi(ADCSRA,ADPS2);
	// disable digital input buffer on all ADC pins (ADC0-ADC7)
	DIDR0 = B11111111;
	// enable ADC
	sbi(ADCSRA,ADEN);
	// start first conversion
	sbi(ADCSRA,ADSC);
	// Init analog comparator
	// select intr on rising edge
	sbi(ACSR,ACIS1);
	sbi(ACSR,ACIS0);
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
	// enable interrupts on output compare
	sbi(TIMSK1,OCIE1A);
	// Init serial
	Serial.begin(9600);
	// Init LED
	// we use LED 13 as an aquisition indicator
	pinMode(13,OUTPUT);
	PORTB&=B11011111;
}

void
loop () {
	unsigned char c;
	// clear ready flag
	rdy = 0;
	// clear sum and trial counter
	s = 0;
	t = 0;
	// select channel 0
	ch = 0;
	ADMUX &= B11110000;
	// set initial position and delay
	n = 0;
	OCR1A = dt;
	// enable analog comparator interrupt
	sbi(ACSR,ACIE);
	// wait for the data to be ready
	while (!rdy);
	// write out data
	Serial.write(0); // sync
	Serial.write(chs); // report current number of channels
	Serial.write(dt); // report current time step
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
		dt = Serial.read();
	}
}
