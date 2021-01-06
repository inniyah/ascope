// Arduino oscilloscope
// Copyright (c) 2021 Alexander Mukhin
// MIT License

#define N 256 // samples in buffer
#define MAXCHS 2 // maximum number of channels (1..4)

// control structure
struct ctl {
	unsigned char samp; // sampling mode (0=RT,1=ET)
	unsigned char trig; // trigger mode (1=normal,0=auto) [RT only]
	unsigned char chs; // current number of channels (1..MAXCHS)
	unsigned char slope; // trigger edge (1=rising,0=falling)
	unsigned char prescale; // timer clock (ET) or ADC clock (RT) prescale
};

// make oscilloscope control word
unsigned char
makecw (struct ctl c) {
	return ((c.samp&1)<<7)+((c.trig&1)<<6)+(((c.chs-1)&3)<<4)+((c.slope&1)<<3)+(c.prescale&7);
}

// parse oscilloscope control word
void
parsecw (unsigned char cw, struct ctl *c) {
	c->prescale=cw&7;
	c->slope=cw>>3&1;
	c->chs=(cw>>4&3)+1;
	c->trig=cw>>6&1;
	c->samp=cw>>7&1;
}
