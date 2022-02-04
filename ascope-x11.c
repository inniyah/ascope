// X11 GUI for Arduino oscilloscope
// Copyright (c) 2021 Alexander Mukhin
// MIT License

// device
#define DEV "/dev/ttyACM0"
// actual input voltage range
#define Vmin -5.0
#define Vmax 5.0
// square size
#define SQ 50
// squares in a quadrant
#define SQX 5
#define SQY 4
// border width
const int B=10;
// channel colors
const int clrs[]={0x00ff00,0xff0000,0x0000ff,0xffffff};

#include "ascope.h"
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
#include <png.h>
#include <errno.h>

// derived constants
const int W=SQ*SQX*2,H=SQ*SQY*2; // oscillogram width and height
const float VDIVX=(Vmax-Vmin)/2/SQX; // volts per X division
const float VDIVY=(Vmax-Vmin)/2/SQY; // volts per Y division
const int SDIV=N/2/SQX; // samples per division
// other constants
const int MAXP=8; // maximum time zoom power [may not exceed log2(N)]
const int POLLTIMO=5000; // poll timeout in milliseconds
const float pi=3.14159265358979323846;

// sample to voltage conversion
float
s2v (unsigned char c) {
	float t=c/255.0;
	return Vmin*(1-t)+Vmax*t;
}

// return time step between samples in microseconds
float
dt (struct ctl cs) {
	if (cs.samp==1) {
		// equivalent-time
		int f[]={1,8,64,256,1024}; // clock division factors
		return f[cs.prescale-1]/16.0;
	} else {
		// real-time
		return (13*(1<<cs.prescale))/16.0;
	}
}

// draw graticule
void
makegrat (Display *dpy, Pixmap pm, GC gc) {
	int i,j; // counters
	int x,y,x1,y1; // coordinates
	XSetForeground(dpy,gc,0xffffff);
	XDrawLine(dpy,pm,gc,W/2,0,W/2,H);
	XDrawLine(dpy,pm,gc,0,H/2,W,H/2);
	XSetForeground(dpy,gc,0x808080);
	for (i=1; i<=SQX; ++i) {
		x=i*SQ;
		for (j=1; j<=4; ++j) {
			x1=j*SQ/5;
			XDrawLine(dpy,pm,gc,W/2+x-x1,H/2-SQ/8,W/2+x-x1,H/2+SQ/8);
			XDrawLine(dpy,pm,gc,W/2-x+x1,H/2-SQ/8,W/2-x+x1,H/2+SQ/8);
		}
		XDrawLine(dpy,pm,gc,W/2+x,0,W/2+x,H);
		XDrawLine(dpy,pm,gc,W/2-x,0,W/2-x,H);
	}
	for (i=1; i<=SQY; ++i) {
		y=i*SQ;
		for (j=1; j<=4; ++j) {
			y1=j*SQ/5;
			XDrawLine(dpy,pm,gc,W/2-SQ/8,H/2+y-y1,W/2+SQ/8,H/2+y-y1);
			XDrawLine(dpy,pm,gc,W/2-SQ/8,H/2-y+y1,W/2+SQ/8,H/2-y+y1);
		}
		XDrawLine(dpy,pm,gc,0,H/2+y,W,H/2+y);
		XDrawLine(dpy,pm,gc,0,H/2-y,W,H/2-y);
	}
}

// draw oscillogram
void
makeosc (Display *dpy, Pixmap pm, GC gc, float buf[MAXCHS][N], int chs, int xy) {
	int ch; // current channel
	int i; // counter
	XPoint pp[N]; // points
	if (xy) {
		// XY mode
		XSetForeground(dpy,gc,clrs[0]|clrs[1]);
		for (i=0; i<N; ++i) {
			pp[i].x=W*(buf[0][i]-Vmin)/(Vmax-Vmin);
			pp[i].y=H*(Vmax-buf[1][i])/(Vmax-Vmin);
		}
		XDrawLines(dpy,pm,gc,pp,N,CoordModeOrigin);
	} else {
		// normal mode
		for (ch=0; ch<chs; ++ch) {
			XSetForeground(dpy,gc,clrs[ch]);
			for (i=0; i<N; ++i) {
				pp[i].x=i*W/N;
				pp[i].y=H*(Vmax-buf[ch][i])/(Vmax-Vmin);
			}
			XDrawLines(dpy,pm,gc,pp,N,CoordModeOrigin);
		}
	}
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

// fill sinc tables
void
fill_sinc (float sinctbl[MAXP+1][N*N]) {
	int p,z; // zoom power and factor
	int k,l,m; // indices
	float *tptr; // pointer
	for (p=0; p<=MAXP; ++p) {
		z=1<<p;
		tptr=sinctbl[p];
		for (k=0; k<N/z; ++k)
			for (l=0; l<z; ++l)
				for (m=0; m<N; ++m)
					*tptr++=sinc(pi*((float)l/z+k-m));
	}
}

// linear interpolation
void
interp_lin (int z, const float *buf, float *zbuf) {
	int k,l; // indices
	float t; // parameter
	for (k=0; k<N/z; ++k)
		for (l=0; l<z; ++l) {
			t=(float)l/z;
			*zbuf++=buf[k]*(1-t)+buf[k+1]*t;
		}
}

// sinc interpolation
void
interp_sinc (int z, const float *tbl, const float *buf, float *zbuf) {
	int k,l,m; // indices
	float s; // sum
	for (k=0; k<N/z; ++k)
		for (l=0; l<z; ++l) {
			s=0.0;
			for (m=0; m<N; ++m)
				s+=(buf[m]-buf[0])**tbl++;
			*zbuf++=s+buf[0];
		}
}

int
main (void) {
	unsigned char rbuf[MAXCHS][N]; // raw data buffer
	unsigned char cw; // oscilloscope control word
	struct ctl cs; // oscilloscope control structure
	unsigned char c; // data sample
	int ch; // channel index
	int n; // sample index
	float vbuf[MAXCHS][N]; // acquired data converted to voltage
	int p=0,zt=1<<p; // time scale zoom power and factor (zt=2^p)
	float zbuf[MAXCHS][N]; // interpolated (zoomed) data
	float sinctbl[MAXP+1][N*N]; // precomputed sinc tables
	int fd; // oscilloscope device file descriptor
	struct termios t; // terminal structure
	struct pollfd pfds[2]; // poll structures
	enum {M_LIN=1,M_RUN=2,M_SNGL=4,M_XY=8} mode=3; // mode of operation
	char str[256]; // string buffer (for status line text and keyboard input)
	int sync=0; // sync flag, means CW is received
	int rdy=0; // data ready flag, means oscillogram is received
	int sendcw=0; // update and send new CW request
	int redraw=0; // redraw oscillogram request
	// X stuff
	Display *dpy;
	Window win;
	XEvent evt;
	int scr;
        GC gc;
	Pixmap pm; // combined pixmap
	Pixmap gpm; // graticule pixmap
	KeySym ks;
	XFontStruct *fs;
	int slh; // status line height
	int ww,wh; // window width and height
	int pw,ph; // pixmap width and height

	// fill sinc tables
	fill_sinc(sinctbl);

	// open device
	fd=open(DEV,O_RDWR);
	if (fd==-1) {
		fprintf(stderr,"Cannot open device\n");
		return 1;
	}
	// set up line
	tcgetattr(fd,&t);
	cfsetispeed(&t,B9600); // 9600 baud input
	cfsetospeed(&t,B9600); // 9600 baud output
	t.c_cflag|=CS8; // 8-bit character
	t.c_cflag&=~(PARENB|CSTOPB); // no parity, one stop bit
	t.c_lflag&=~(ICANON|IEXTEN|ISIG); // ignore special characters
	t.c_lflag&=~ECHO; // do not echo
	t.c_iflag&=~(ICRNL|INLCR|IGNCR); // do nothing with NL and CR
	t.c_iflag&=~(IXON|IXOFF); // do not accept or send START/STOP
	t.c_oflag&=~OPOST; // do not postprocess output
	t.c_cflag|=CLOCAL; // line without modem control
	t.c_cc[VMIN]=1; // minimum 1 character for a completed read
	tcsetattr(fd,TCSANOW,&t);

	// init X
	dpy=XOpenDisplay(NULL);
	if (dpy==NULL) {
		fprintf(stderr,"Cannot open display\n");
		return 1;
	}
	scr=DefaultScreen(dpy);
	gc=DefaultGC(dpy,scr);
	fs=XQueryFont(dpy,XGContextFromGC(gc));
	slh=fs->ascent+fs->descent;
	ww=W+2*B;
	wh=H+slh+2*B;
	win=XCreateSimpleWindow(dpy,RootWindow(dpy,scr),0,0,ww,wh,0,0,0);
	XSelectInput(dpy,win,ExposureMask|KeyPressMask|ButtonPressMask);
	XDefineCursor(dpy,win,XCreateFontCursor(dpy,XC_crosshair));
	XStoreName(dpy,win,"ascope");
	XMapWindow(dpy,win);
	pw=W+1;
	ph=H+slh+1;
	pm=XCreatePixmap(dpy,win,pw,ph,DefaultDepth(dpy,scr));
	gpm=XCreatePixmap(dpy,win,pw,ph,DefaultDepth(dpy,scr));
	XFlush(dpy);

	// prepare pollfd structures
	pfds[0].fd=fd;
	pfds[0].events=POLLIN|POLLERR;
	pfds[1].fd=ConnectionNumber(dpy);
	pfds[1].events=POLLIN;

	// flush input buffer as it might contain invalid data
	// received before the terminal settings took effect
	tcflush(fd,TCIFLUSH);

	// prepare graticule
	makegrat(dpy,gpm,gc);

	// event loop
	while (1) {
		// wait for events
		if (!poll(pfds,2,POLLTIMO)) {
			// poll() timed out
			if (sync && mode&M_RUN) {
				// clear ready flag
				rdy=0;
				// request redraw
				redraw=1;
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
				parsecw(cw,&cs);
				// read data ready flag
				read(fd,&c,1);
				// set flags
				sync=1;
				rdy=(c==1)?1:0;
				// read data buffers
				if (rdy) {
					for (ch=0; ch<cs.chs; ++ch)
						for (n=0; n<N; ++n) {
							read(fd,&c,1);
							rbuf[ch][n]=c;
							// convert sample to voltage
							vbuf[ch][n]=s2v(c);
						}
				}
				// freeze if we're in single sweep mode
				if (mode&M_SNGL) {
					mode&=~M_RUN;
					// remove device fd
					// from the poll structure
					// to ignore serial events
					pfds[0].fd=-1;
					// change window name
					XStoreName(dpy,win,\
					"ascope [frozen]");
					// quit single sweep mode as well
					mode&=~M_SNGL;
				}
				// request redraw
				redraw=1;
			}
		}
		// process X events, if any
		while (XPending(dpy)) {
			// read X event queue
			XNextEvent(dpy,&evt);
			if (evt.type==Expose) {
				// show pixmap
				XCopyArea(dpy,pm,win,gc,0,0,pw,ph,B,B);
			}
			if (evt.type==KeyPress) {
				XLookupString(&evt.xkey,str,1,&ks,NULL);
				if (ks==XK_q) {
					// quit
					return 0;
				}
				if (sync && mode&M_RUN && ks==XK_m) {
					// toggle sampling mode
					if (cs.samp==1) {
						// from et to rt
						cs.samp=0; // rt
						cs.prescale=2; // fastest rate
					} else {
						// from rt to et
						cs.samp=1; // et
						cs.trig=1; // normal triggering
						cs.prescale=1; // fastest rate
					}
					sendcw=1;
				}
				if (sync && mode&M_RUN && isdigit((int)str[0])) {
					// set number of channels
					str[1]=0;
					cs.chs=atoi(str);
					if (cs.chs<1) cs.chs=1;
					if (cs.chs>MAXCHS) cs.chs=MAXCHS;
					if (cs.chs!=2) mode&=~M_XY;
					sendcw=1;
				}
				if (sync && mode&M_RUN && ks==XK_plus) {
					// increase sampling rate
					if (cs.samp==1) {
						// equivalent-time
						if (cs.prescale>1) {
							--cs.prescale;
							sendcw=1;
						}
					} else {
						// real-time
						if (cs.prescale>2) {
							--cs.prescale;
							sendcw=1;
						}
					}
				}
				if (sync && mode&M_RUN && ks==XK_minus) {
					// decrease sampling rate
					if (cs.samp==1) {
						// equivalent-time
						if (cs.prescale<5) {
							++cs.prescale;
							sendcw=1;
						}
					} else {
						// real-time
						if (cs.prescale<7) {
							++cs.prescale;
							sendcw=1;
						}
					}
				}
				if (sync && cs.samp==0 && mode&M_RUN && ks==XK_a) {
					// set auto-trigger mode
					cs.trig=0;
					sendcw=1;
				}
				if (sync && mode&M_RUN && ks==XK_slash) {
					// trigger on rising edge
					cs.slope=1;
					cs.trig=1;
					sendcw=1;
				}
				if (sync && mode&M_RUN && ks==XK_backslash) {
					// trigger on falling edge
					cs.slope=0;
					cs.trig=1;
					sendcw=1;
				}
				if (sync && mode&M_RUN && ks==XK_Right) {
					// increase time zoom
					if (p<MAXP) {
						++p;
						zt=1<<p;
					}
					redraw=1;
				}
				if (sync && mode&M_RUN && ks==XK_Left) {
					// decrease time zoom
					if (p>0) {
						--p;
						zt=1<<p;
					}
					redraw=1;
				}
				if (sync && mode&M_RUN && p && ks==XK_i) {
					// toggle interpolation mode
					mode^=M_LIN;
				}
				if (sync && mode&M_RUN && cs.chs==2 && ks==XK_x) {
					// toggle XY mode
					mode^=M_XY;
				}
				if (ks==XK_space) {
					// toggle run mode
					mode^=M_RUN;
					if (mode&M_RUN) {
						// return device fd
						// to the poll structure
						pfds[0].fd=fd;
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
						pfds[0].fd=-1;
						// change window name
						XStoreName(dpy,win,"ascope [frozen]");
					}
				}
				if (ks==XK_s) {
					// enter single sweep mode
					mode|=M_SNGL;
					// change window name
					XStoreName(dpy,win,"ascope [single-sweep]");
				}
				if (rdy && ks==XK_d) {
					// dump raw buffer to a file
					FILE *of; // output file
					const char *ofname="out.dump"; // file name
					of=fopen(ofname,"w");
					if (of==NULL) {
						fprintf(stderr,
							"Can't open %s: %s\n",
							ofname,strerror(errno));
					} else {
						for (n=0; n<N; ++n) {
							for (ch=0; ch<cs.chs; ++ch)
								fprintf(of,
								"%hhu ",rbuf[ch][n]);
							fprintf(of,"\n");
						}
						fclose(of);
						printf("wrote %s\n",ofname);
					}
				}
				if (rdy && ks==XK_w) {
					// write oscillogram to a file
					XImage *ximg; // X image
					png_image pimg; // PNG control structure
					png_bytep buf,ptr; // PNG image data
					FILE *of; // output file
					const char *ofname="out.png"; // file name
					int i,j; // counters
					unsigned int p; // pixel
					// prepare X image
					ximg=XGetImage(dpy,pm,0,0,pw,ph,0xffffffff,ZPixmap);
					// prepare PNG image
					memset(&pimg,0,(sizeof pimg));
					pimg.version=PNG_IMAGE_VERSION;
					pimg.width=ww;
					pimg.height=wh;
					pimg.format=PNG_COLOR_TYPE_RGB;
					buf=calloc(1,PNG_IMAGE_SIZE(pimg));
					// copy image
					ptr=buf;
					for (j=0; j<ph; ++j)
						for (i=0; i<pw; ++i) {
							ptr=buf+3*(ww*(j+B)+(i+B));
							p=XGetPixel(ximg,i,j);
							// assume ximg in RGB order
							*ptr++=(p&0xff0000)>>16;
							*ptr++=(p&0x00ff00)>>8;
							*ptr++=p&0x0000ff;
						}
					// save to file
					of=fopen(ofname,"w");
					if (of==NULL) {
						fprintf(stderr,
							"Can't open %s: %s\n",
							ofname,strerror(errno));
					} else {
						png_image_write_to_stdio(&pimg,of,0,buf,0,NULL);
						fclose(of);
						printf("wrote %s\n",ofname);
					}
					// cleanup
					free(buf);
					XDestroyImage(ximg);
				}
			}
			if (sync && evt.type==ButtonPress) {
				// show time and voltage below mouse pointer
				float x,y;
				x=evt.xbutton.x-B;
				y=evt.xbutton.y-B;
				if (x>=0 && x<=W && y>=0 && y<=H) {
					if (mode&M_XY) {
						float vx,vy;
						vx=(Vmin+(Vmax-Vmin)*x/W);
						vy=(Vmax-(Vmax-Vmin)*y/H);
						printf("%.2f V, %.2f V\n",vx,vy);
					} else {
						float t,v;
						t=(x/W)*N*dt(cs)/zt;
						v=(Vmax-(Vmax-Vmin)*y/H);
						printf("%.1f us, %.2f V\n",t,v);
					}
				}
			}
		}
		// process flags
		// update and send the new CW
		if (sendcw) {
			// make control word
			cw=makecw(cs);
			// send it to the device
			write(fd,&cw,1);
			tcflush(fd,TCOFLUSH);
			// invalidate data and wait for new sync
			sync=rdy=0;
			// clear flag
			sendcw=0;
		}
		// redraw
		if (redraw && sync) {
			// copy graticule
			XCopyArea(dpy,gpm,pm,gc,0,0,pw,ph,0,0);
			// draw oscillogram
			if (rdy) {
				// do interpolation (zoom)
				for (ch=0; ch<cs.chs; ++ch)
					if (zt>1 && mode&M_LIN)
						// linear interpolation
						interp_lin(zt,vbuf[ch],zbuf[ch]);
					else if (zt>1)
						// sinc interpolation
						interp_sinc(zt,sinctbl[p], \
						vbuf[ch],zbuf[ch]);
					else
						// copy
						memcpy(zbuf[ch],vbuf[ch], \
						N*sizeof(float));
				makeosc(dpy,pm,gc,zbuf,cs.chs,mode&M_XY);
			}
			// draw status line
			if (mode&M_XY)
				if (zt==1)
					snprintf(str,256,
					"%.2f V/divX, %.2f V/divY, "
					"%.2f ms %cT, "
					"%c",
					VDIVX,VDIVY,
					N*dt(cs)/1000/zt,cs.samp?'E':'R',
					cs.trig?cs.slope?'/':'\\':'A');
				else
					snprintf(str,256,
					"%.2f V/divX, %.2f V/divY, "
					"%.2f ms %cT (x%d%c), "
					"%c",
					VDIVX,VDIVY,
					N*dt(cs)/1000/zt,cs.samp?'E':'R',
					zt,
					(mode&M_LIN)?'L':'S',
					cs.trig?cs.slope?'/':'\\':'A');
			else
				if (zt==1)
					snprintf(str,256,
					"%.1f us/div %cT, "
					"%.2f V/div, "
					"%d ch%s, "
					"%c",
					SDIV*dt(cs),
					cs.samp?'E':'R',
					VDIVY,
					cs.chs,cs.chs>1?"s":"",
					cs.trig?cs.slope?'/':'\\':'A');
				else
					snprintf(str,256,
					"%.1f us/div %cT (x%d%c), "
					"%.1f V/div, "
					"%d ch%s, "
					"%c",
					SDIV*dt(cs)/zt,
					cs.samp?'E':'R',
					zt,
					(mode&M_LIN)?'L':'S',
					VDIVY,
					cs.chs,cs.chs>1?"s":"",
					cs.trig?cs.slope?'/':'\\':'A');
			XSetForeground(dpy,gc,0xffffff);
			XDrawString(dpy,pm,gc,0,ph-1,str,strlen(str));
			// send itself an exposure event
			// to display the oscillogram
			evt.type=Expose;
			XSendEvent(dpy,win,False,0,&evt);
			XFlush(dpy);
			// clear flag
			redraw=0;
		}
	}
}
