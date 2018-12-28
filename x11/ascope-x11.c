// X11 GUI for Arduino oscilloscope
// Copyright (c) 2018 Alexander Mukhin
// MIT License

#define N 256 // number of samples in buffer
#define MAXCHS 2 // maximum number of channels
const int clrs[MAXCHS]={0x00ff00,0xff0000}; // channel colors
#define W 512 // window width
#define H 256 // window height
#define V_MIN 0.0 // voltage for ADC reading 0
#define V_MAX 5.0 // voltage for ADC reading 255
#define VDIV 0.5 // horizontal grid step in volts per division
#define SDIV 8 // vertical grid step in samples per division
#define POLLTIMO 5000 // poll timeout in milliseconds
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
#include <ctype.h>

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

// return time step between samples in microseconds
float
dt (unsigned char prescale) {
	int f[] = {1,8,64,256,1024}; // clock division factors
	return f[prescale-1]/16.0;
}

// draw oscillogram on a pixmap
void
makeosc (Display *dpy, Pixmap pm, GC gc, float buf[MAXCHS][N], int chs) {
	int ch; // current channel
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
	for (i=1; i<=N/SDIV; ++i) {
		x = i*SDIV*W/N-1;
		XDrawLine(dpy,pm,gc,x,0,x,H-1);
	}	
	// draw zero voltage axis
	XSetForeground(dpy,gc,0x808080);
	y = (H-1)*(1+V_MIN/(V_MAX-V_MIN));
	XDrawLine(dpy,pm,gc,0,y,W-1,y);
	if (!buf)
		// return if no data available
		// we use this to clear screen
		return;
	// draw waveforms
	for (ch=0; ch<chs; ++ch) {
		XSetForeground(dpy,gc,clrs[ch]);
		for (i=0; i<N; ++i) {
			x = i*W/N;
			y = (H-1)*(1-buf[ch][i]);
			if (i)
				XDrawLine(dpy,pm,gc,xprev,yprev,x,y);
			else
				XDrawPoint(dpy,pm,gc,0,y);
			xprev = x;
			yprev = y;
		}
	}
}

int
main (void) {
	unsigned char rbuf[MAXCHS][N]; // raw data buffer
	unsigned char cw; // oscilloscope control word
	unsigned char chs; // number of channels
	unsigned char slope; // trigger slope
	unsigned char prescale; // timer clock prescale value
	unsigned char c; // data sample
	int ch; // channel index
	int n; // sample index
	float sbuf[MAXCHS][N]; // raw data scaled to [0,1]
	int fd; // oscilloscope device file descriptor
	struct termios t; // terminal structure
	struct pollfd pfds[2]; // poll structures
	enum {O_RUN=0x1} mode=1; // mode of operation
	char str[256]; // string buffer (for status line text and keyboard input)
	int rdy=0; // ready flag, means oscillogram is received and displayed
	int sendcw; // update and send new CW flag
	// X stuff
	Display *dpy;
	Window win;
	XEvent evt;
	int scr;
        GC gc;
	Pixmap pm;
	KeySym ks;
	XFontStruct *fs;
	int slh; // status line height

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
	gc = DefaultGC(dpy,scr);
	fs = XQueryFont(dpy,XGContextFromGC(gc));
	slh = fs->ascent+fs->descent;
	win = XCreateSimpleWindow(dpy,RootWindow(dpy,scr),0,0,W,H+slh,0,0,0);
	XSelectInput(dpy,win,ExposureMask|KeyPressMask|ButtonPressMask);
	XDefineCursor(dpy,win,XCreateFontCursor(dpy,XC_crosshair));
	XStoreName(dpy,win,"ascope");
	XMapWindow(dpy,win);
	pm = XCreatePixmap(dpy,win,W,H+slh,DefaultDepth(dpy,scr));
	XFillRectangle(dpy,pm,gc,0,0,W,H+slh);
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
				// clear pixmap
				XSetForeground(dpy,gc,0x000000);
				XFillRectangle(dpy,pm,gc,0,0,W,H+slh);
				// draw empty grid
				makeosc(dpy,pm,gc,NULL,chs);
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
				// read and parse device control word
				read(fd,&cw,1);
				parsecw(cw,&prescale,&slope,&chs);
				// read data buffers
				for (ch=0; ch<chs; ++ch)
					for (n=0; n<N; ++n) {
						read(fd,&c,1);
						rbuf[ch][n] = c;
						// fill scaled buffer
						sbuf[ch][n] = c/255.0;
					}
				// clear pixmap
				XSetForeground(dpy,gc,0x000000);
				XFillRectangle(dpy,pm,gc,0,0,W,H+slh);
				// draw oscillogram
				makeosc(dpy,pm,gc,sbuf,chs);
				// draw status line
				snprintf(str,256,
				         "%.1f V/div, %.1f us/div, "
					 "%d ch%s, %c",
					 VDIV,SDIV*dt(prescale),
					 chs,chs>1?"s":"",
					 slope?'/':'\\');
				XSetForeground(dpy,gc,0xffffff);
				XDrawString(dpy,pm,gc,0,H+slh-1,str,strlen(str));
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
				XCopyArea(dpy,pm,win,gc,0,0,W,H+slh,0,0);
			}
			if (evt.type==KeyPress) {
				XLookupString(&evt.xkey,str,1,&ks,NULL);
				// clear update CW flag
				sendcw = 0;
				if (ks==XK_q) {
					// quit
					return 0;
				}
				if (rdy && mode&O_RUN && isdigit(str[0])) {
					// set number of channels
					str[1] = 0;
					chs = atoi(str);
					if (chs<1)
						chs = 1;
					if (chs>MAXCHS)
						chs = MAXCHS;
					// request sending of the new CW
					sendcw = 1;
				}
				if (rdy && mode&O_RUN && ks==XK_Down) {
					// decrease time step
					if (prescale>1)
						--prescale;
					// request sending of the new CW
					sendcw = 1;
				}
				if (rdy && mode&O_RUN && ks==XK_Up) {
					// increase time step
					if (prescale<5)
						++prescale;
					// request sending of the new CW
					sendcw = 1;
				}
				if (rdy && mode&O_RUN && ks==XK_slash) {
					// trigger on rising edge
					slope = 1;
					// request sending of the new CW
					sendcw = 1;
				}
				if (rdy && mode&O_RUN && ks==XK_backslash) {
					// trigger on falling edge
					slope = 0;
					// request sending of the new CW
					sendcw = 1;
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
					for (ch=0; ch<chs; ++ch)
						for (n=0; n<N; ++n)
							fprintf(stderr,\
							"%hhu\n",rbuf[ch][n]);
					fflush(stderr);
				}
				// update and send the new CW
				if (sendcw) {
					// make control word
					cw = makecw(prescale,slope,chs);
					// send it to the device
					write(fd,&cw,1);
					tcflush(fd,TCOFLUSH);
				}
			}
			if (rdy && evt.type==ButtonPress) {
				// show time and voltage below mouse pointer
				float x,y;
				x = evt.xbutton.x;
				y = evt.xbutton.y;
				if (x<W && y<H) {
					float t,v;
					t = (x/W)*N*dt(prescale);
					v = V_MAX-(V_MAX-V_MIN)*y/(H-1);
					printf("%.1f us, %.2f V\n",t,v);
				}
			}
		}
	}
}
