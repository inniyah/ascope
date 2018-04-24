		.text
		.globl	clracif
clracif:	push	r24
		in	r24,0x30
		ori	r24,0x18
		out	0x30,r24

L:		in	r24,0x30
		andi	r24,0x10
		brne	L
		sei
		pop	r24
		ret
