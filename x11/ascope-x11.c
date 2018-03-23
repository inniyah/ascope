// X11 GUI for Arduino oscilloscope
// by Alexander Mukhin
// Public domain

#define N 256 // number of samples in buffer
#define MAXP 8 // maximum zoom power, should not exceed log2(N)
#define W 512 // window width
#define H 256 // window height
#define V_MIN -5.0 // voltage for ADC reading 0
#define V_MAX 5.0 // voltage for ADC reading 255
#define VDIV 1.0 // horizontal grid step in volts per division
#define SDIV 8 // vertical grid step in samples per division
#define POLLTIMO 500 // poll timeout in milliseconds
#define ODEV "/dev/ttyACM0" // oscilloscope device file

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <poll.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define M_PI 3.14159265358979323846

// draw oscillogram on a pixmap
void
mkosc (Display *dpy, Pixmap pm, GC gc, float *buf, int Z) {
	int i; // counter
	int x,xprev,y,yprev; // coordinates
	// clear pixmap
	XSetForeground(dpy,gc,0x000000);
	XFillRectangle(dpy,pm,gc,0,0,W,H);
	// draw grid lines
	XSetForeground(dpy,gc,0x404040);
	for (i=1; i<=floor((V_MAX-V_MIN)/VDIV); ++i) {
		y = i*VDIV*H/(V_MAX-V_MIN)-1;
		XDrawLine(dpy,pm,gc,0,y,W-1,y);
	}
	for (i=1; i<=N/(Z*SDIV); ++i) {
		x = i*Z*SDIV*W/N-1;
		XDrawLine(dpy,pm,gc,x,0,x,H-1);
	}	
	// draw zero axis
	XSetForeground(dpy,gc,0x808080);
	y = (H-1)*(1+V_MIN/(V_MAX-V_MIN));
	XDrawLine(dpy,pm,gc,0,y,W-1,y);
	// draw waveform
	XSetForeground(dpy,gc,0x00ff00);
	if (!buf)
		// return if no data available
		// we use this to clear screen
		return;
	for (i=0; i<N; ++i) {
		x = i*W/N;
		y = (H-1)*(1-buf[i]);
		if (i)
			XDrawLine(dpy,pm,gc,xprev,yprev,x,y);
		else
			XDrawPoint(dpy,pm,gc,0,y);
		xprev = x;
		yprev = y;
	}
}

// get sampling rate in ksps for the given prescale factor
float
ksps (int prescale) {
	return 16000.0/(13*(1<<prescale));
}

// sinus cardinalis
float
sinc (float x) {
	const float eps=0.001;
	if (fabs(x)>eps)
		return sinf(x)/x;
	else
		return 1.0;
}

// precomputed sinc tables
static float sinctbl[MAXP+1][N*N];

int
main (void) {
	unsigned char rbuf[N]; // raw data buffer
	unsigned char prescale; // ADC clock prescale factor
	unsigned char c; // data sample
	float sbuf[N]; // raw data scaled to [0,1]
	float zbuf[N],*zptr; // interpolated (zoomed) data and a pointer
	int p,Z; // zoom power and factor (Z=2^p)
	int k,l,m; // indices
	float *tptr; // pointer to sinc tables
	float s; // sum
	int fd; // oscilloscope device file descriptor
	struct termios t; // terminal structure
	struct pollfd pfds[2]; // poll structures
	enum {O_LIN=0x1,O_RUN=0x2} mode=3; // mode of operation
	char sl[N]; // buffer for status line text
	int rdy=0; // ready flag, means oscillogram is received and displayed
	// X stuff
	Display *dpy;
	Window win;
	XEvent evt;
	int scr;
        GC gc;
	Pixmap pm;
	KeySym ks;

	// fill sinc tables
	for (p=0; p<=MAXP; ++p) {
		Z = 1<<p;
		tptr = sinctbl[p];
		for (k=0; k<N/Z; ++k)
			for (l=0; l<Z; ++l)
				for (m=0; m<N; ++m)
					*tptr++ = sinc(M_PI*((float)l/Z+k-m));
	}

	// init zoom factors
	p = 0;
	Z = 1<<p;

	// open device
	fd = open(ODEV,O_RDWR);
	if (fd==-1) {
		fprintf(stderr,"Cannot open device\n");
		return 1;
	}
	// put it to non-canonical mode
	tcgetattr(fd,&t);
	t.c_iflag &= ~(ICRNL|IXON);
	t.c_oflag &= ~OPOST;
	t.c_cflag &= ~HUPCL;
	t.c_lflag &= ~(ICANON|ECHO|IEXTEN|ISIG);
	tcsetattr(fd,TCSANOW,&t);

	// init X
	dpy = XOpenDisplay(NULL);
	if (dpy==NULL) {
		fprintf(stderr,"Cannot open display\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	win = XCreateSimpleWindow(dpy,RootWindow(dpy,scr),0,0,W,H,0,0,0);
	XSelectInput(dpy,win,ExposureMask|KeyPressMask|ButtonPressMask);
	XDefineCursor(dpy,win,XCreateFontCursor(dpy,XC_crosshair));
	XStoreName(dpy,win,"ascope");
	XMapWindow(dpy,win);
	gc = DefaultGC(dpy,scr);
	pm = XCreatePixmap(dpy,win,W,H,DefaultDepth(dpy,scr));
	XFillRectangle(dpy,pm,gc,0,0,W,H);
	XFlush(dpy);

	// prepare pollfd structures
	pfds[0].fd = fd;
	pfds[0].events = POLLIN|POLLERR;
	pfds[1].fd = ConnectionNumber(dpy);
	pfds[1].events = POLLIN;

	// flush input buffer as it might contain invalid data
	// received before the terminal settings took effect
	tcflush(fd,TCIFLUSH);

	// event loop
	while (1) {
		// wait for events
		if (!poll(pfds,2,POLLTIMO)) {
			// poll() timed out
			if (mode&O_RUN) {
				// clear previous oscillogram
				mkosc(dpy,pm,gc,NULL,Z);
				// send itself an exposure event
				evt.type = Expose;
				XSendEvent(dpy,win,False,0,&evt);
				// clear ready flag
				rdy = 0;
			}
		}
		// exit if device reported error
		if (pfds[0].revents&POLLERR)
			return 2;
		// read serial, if data available
		if (pfds[0].revents&POLLIN) {
			// wait for sync
			read(fd,&c,1);
			if (c==0) {
				// got sync
				// read prescale value
				read(fd,&prescale,1);
				// fill the raw and scaled buffers
				for (m=0; m<N; ++m) {
					read(fd,&c,1);
					rbuf[m] = c;
					sbuf[m] = c/255.0;
				}
				// do interpolation (zoom)
				zptr = zbuf;
				if (mode&O_LIN) {
					// linear interpolation
					for (k=0; k<N/Z; ++k)
						for (l=0; l<Z; ++l) {
							float t=(float)l/Z;
							*zptr++ = \
							sbuf[k]*(1-t)+sbuf[k+1]*t;
						}
				} else {
					// sinc interpolation
					tptr = sinctbl[p];
					for (k=0; k<N/Z; ++k)
						for (l=0; l<Z; ++l) {
							s = 0.0;
							for (m=0; m<N; ++m)
								s += \
								(sbuf[m]-sbuf[0])\
								**tptr++;
							*zptr++ = s+sbuf[0];
						}
				}
				// draw oscillogram
				mkosc(dpy,pm,gc,zbuf,Z);
				// draw status line
				if (Z==1)
					snprintf(sl,256,\
					"%.1f V/div, %.0f us/div", \
					VDIV,SDIV*1000/ksps(prescale));
				else
					snprintf(sl,256,\
					"%.1f V/div, %.0f us/div, " \
					"%dx (%s)", \
					VDIV,SDIV*1000/ksps(prescale), \
					Z,(mode&O_LIN)?"linear":"sinc");
				XSetForeground(dpy,gc,0xffffff);
				XDrawString(dpy,pm,gc,0,H-1,sl,strlen(sl));
				// send itself an exposure event
				// to display the oscillogram
				evt.type = Expose;
				XSendEvent(dpy,win,False,0,&evt);
				// raise ready flag
				rdy = 1;
			}
		}
		// process X events, if any
		while (XPending(dpy)) {
			// read X event queue
			XNextEvent(dpy,&evt);
			if (evt.type==Expose) {
				// show pixmap
				XCopyArea(dpy,pm,win,gc,0,0,W,H,0,0);
			}
			if (evt.type==KeyPress) {
				XLookupString(&evt.xkey,NULL,0,&ks,NULL);
				if (ks==XK_q) {
					// quit
					return 0;
				}
				if (rdy && mode&O_RUN && ks==XK_Down) {
					// decrease sampling rate
					if (prescale<7) {
						++prescale;
						// send it to the device
						write(fd,&prescale,1);
						tcflush(fd,TCOFLUSH);
					}
				}
				if (rdy && mode&O_RUN && ks==XK_Up) {
					// increase sampling rate
					if (prescale>2) {
						--prescale;
						// send it to the device
						write(fd,&prescale,1);
						tcflush(fd,TCOFLUSH);
					}
				}
				if (rdy && mode&O_RUN && ks==XK_Left) {
					// decrease zoom power
					if (p>0) {
						--p;
						Z = 1<<p;
					}
				}
				if (rdy && mode&O_RUN && ks==XK_Right) {
					// increase zoom power
					if (p<MAXP) {
						++p;
						Z = 1<<p;
					}
				}
				if (rdy && mode&O_RUN && p && ks==XK_i) {
					// toggle interpolation mode
					mode ^= O_LIN;
				}
				if (ks==XK_space) {
					// toggle run mode
					mode ^= O_RUN;
					if (mode&O_RUN) {
						// return device fd
						// to the poll structure
						pfds[0].fd = fd;
						// flush input buffer
						// when resuming operation
						// to get rid of the data
						// received while we were sleeping
						tcflush(fd,TCIFLUSH);
						// reset window title
						XStoreName(dpy,win,"ascope");
					} else {
						// remove device fd
						// from the poll structure
						// to ignore serial events
						pfds[0].fd = -1;
						// change window name
						XStoreName(dpy,win,\
						"ascope [frozen]");
					}
				}
				if (rdy && ks==XK_d) {
					// dump raw buffer to stderr
					for (k=0; k<N; ++k)
						fprintf(stderr,"%hhu\n",rbuf[k]);
					fflush(stderr);
				}
			}
			if (rdy && evt.type==ButtonPress) {
				// show time and voltage at the point
				float x,y;
				x = evt.xbutton.x;
				y = evt.xbutton.y;
				if (x<W && y<H) {
					float t,v;
					t = (x/W)*N*1000.0/(Z*ksps(prescale));
					v = V_MAX-(V_MAX-V_MIN)*y/(H-1);
					printf("%.1f us, %.2f V\n",t,v);
				}
			}
		}
	}
}
