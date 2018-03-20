// ADC ISR for oscilloscope

	.text
	.globl	__vector_21
__vector_21:
	// save r24, SREG, and Z
	push	r24
	in	r24,0x3f
	push	r24
	push	r30
	push	r31
	// load ADCH
	lds	r24,0x79
	// compute buf[n]
	lds	r30,n
	ldi	r31,0
	subi	r30,lo8(-(buf))
	sbci	r31,hi8(-(buf))
	// store ADCH in buf[n] and increment n
	st	Z+,r24
	// restore incremented n
	subi	r30,lo8(buf)
	// save incremented n
	sts	n,r30
	// quit if incremented n is not zero
	brne	Q
	// if incremented n is zero
	// stop ADC
	lds	r24,0x7a	// load ADCSRA
	andi	r24,0b01111111	// clear ADEN bit
	sts	0x7a,r24	// store ADCSRA
	// raise ready flag
	ldi	r24,1
	sts	rdy,r24
	// turn off aquisition LED
	cbi	0x5,5
Q:	// restore Z, SREG, and r24
	pop	r31
	pop	r30
	pop	r24
	out	0x3f,r24
	pop	r24
	// we're done
	reti
