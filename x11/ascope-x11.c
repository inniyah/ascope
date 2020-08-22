// X11 GUI for Arduino oscilloscope
// Copyright (c) 2020 Alexander Mukhin
// MIT License

#include "ascope.h"

// conversion circuit
const float Vmin=-5.0,Vmax=5.0; // input voltage range
// appearance
const float VDIV=1.0; // volts per division
const int SDIV=8; // samples per division
const int W=512,H=256; // oscillogram width and height
const int B=10; // border width
const int clrs[MAXCHS]={0x00ff00,0xff0000}; // channel colors
const int MAXP=8; // maximum time zoom power [may not exceed log2(N)]
// device
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
#include <png.h>
#include <errno.h>

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

// draw oscillogram on a pixmap
void
makeosc (Display *dpy, Pixmap pm, GC gc, float buf[MAXCHS][N], int chs, int zt, int zv) {
	int ch; // current channel
	int i; // counter
	int x,xprev,y,yprev; // coordinates
	float svmin=Vmin/zv,svmax=Vmax/zv; // scaled voltage display limits
	// draw grid lines
	XSetForeground(dpy,gc,0x404040);
	for (i=1; i<=floor(svmax/VDIV); ++i) {
		y=(H-1)*(svmax-i*VDIV)/(svmax-svmin);
		XDrawLine(dpy,pm,gc,0,y,W-1,y);
	}
	for (i=1; i<=floor(-svmin/VDIV); ++i) {
		y=(H-1)*(svmax+i*VDIV)/(svmax-svmin);
		XDrawLine(dpy,pm,gc,0,y,W-1,y);
	}
	for (i=0; i<=N/(zt*SDIV); ++i) {
		x=i*zt*SDIV*(W-1)/N;
		XDrawLine(dpy,pm,gc,x,0,x,H-1);
	}	
	// draw zero voltage axis
	XSetForeground(dpy,gc,0x808080);
	y=(H-1)*svmax/(svmax-svmin);
	XDrawLine(dpy,pm,gc,0,y,W-1,y);
	if (!buf)
		// return if no data available
		// we use this to clear screen
		return;
	// draw waveforms
	for (ch=0; ch<chs; ++ch) {
		XSetForeground(dpy,gc,clrs[ch]);
		for (i=0; i<N; ++i) {
			x=i*W/N;
			y=(H-1)*(svmax-buf[ch][i])/(svmax-svmin);
			if (i)
				XDrawLine(dpy,pm,gc,xprev,yprev,x,y);
			else
				XDrawPoint(dpy,pm,gc,0,y);
			xprev=x;
			yprev=y;
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
	int zv=1; // voltage scale zoom factor
	float zbuf[MAXCHS][N]; // interpolated (zoomed) data
	float sinctbl[MAXP+1][N*N]; // precomputed sinc tables
	int fd; // oscilloscope device file descriptor
	struct termios t; // terminal structure
	struct pollfd pfds[2]; // poll structures
	enum {O_LIN=0x1,O_RUN=0x2,O_SNGL=0x4} mode=3; // mode of operation
	char str[256]; // string buffer (for status line text and keyboard input)
	int sync=0; // sync flag, means CW is received
	int rdy=0; // data ready flag, means oscillogram is received
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
	int ww,wh; // window width and height
	int pw,ph; // pixmap width and height

	// fill sinc tables
	fill_sinc(sinctbl);

	// open device
	fd=open(ODEV,O_RDWR);
	if (fd==-1) {
		fprintf(stderr,"Cannot open device\n");
		return 1;
	}
	// put it to non-canonical mode
	tcgetattr(fd,&t);
	t.c_iflag&=~(ICRNL|IXON);
	t.c_oflag&=~OPOST;
	t.c_cflag&=~HUPCL;
	t.c_lflag&=~(ICANON|ECHO|IEXTEN|ISIG);
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
	pw=W;
	ph=H+slh;
	pm=XCreatePixmap(dpy,win,pw,ph,DefaultDepth(dpy,scr));
	XFillRectangle(dpy,pm,gc,0,0,pw,ph);
	XFlush(dpy);

	// prepare pollfd structures
	pfds[0].fd=fd;
	pfds[0].events=POLLIN|POLLERR;
	pfds[1].fd=ConnectionNumber(dpy);
	pfds[1].events=POLLIN;

	// flush input buffer as it might contain invalid data
	// received before the terminal settings took effect
	tcflush(fd,TCIFLUSH);

	// event loop
	while (1) {
		// wait for events
		poll(pfds,2,-1);
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
					// do interpolation (zoom)
					for (ch=0; ch<cs.chs; ++ch)
						if (zt>1 && mode&O_LIN)
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
				}
				// clear pixmap
				XSetForeground(dpy,gc,0x000000);
				XFillRectangle(dpy,pm,gc,0,0,pw,ph);
				if (rdy)
					// draw oscillogram
					makeosc(dpy,pm,gc,zbuf,cs.chs,zt,zv);
				else
					// or empty grid
					makeosc(dpy,pm,gc,NULL,cs.chs,zt,zv);
				// draw status line
				if (zt==1 && zv==1)
					snprintf(str,256,
					"%.1f V/div, "
					"%.1f us/div %cT, "
					"%d ch%s, "
					"%c",
					VDIV,
					SDIV*dt(cs),
					cs.samp?'E':'R',
					cs.chs,cs.chs>1?"s":"",
					cs.trig?cs.slope?'/':'\\':'A');
				else if (zt>1 && zv==1)
					snprintf(str,256,
					"%.1f V/div, "
					"%.1f us/div (%dx %s) %cT, "
					"%d ch%s, "
					"%c",
					VDIV,
					SDIV*dt(cs),zt,(mode&O_LIN)?"linear":"sinc",
					cs.samp?'E':'R',
					cs.chs,cs.chs>1?"s":"",
					cs.trig?cs.slope?'/':'\\':'A');
				else if (zt==1 && zv>1)
					snprintf(str,256,
					"%.1f V/div (%dx), "
					"%.1f us/div %cT, "
					"%d ch%s, "
					"%c",
					VDIV,zv,
					SDIV*dt(cs),
					cs.samp?'E':'R',
					cs.chs,cs.chs>1?"s":"",
					cs.trig?cs.slope?'/':'\\':'A');
				else
					snprintf(str,256,
					"%.1f V/div (%dx), "
					"%.1f us/div (%dx %s) %cT, "
					"%d ch%s, "
					"%c",
					VDIV,zv,
					SDIV*dt(cs),zt,(mode&O_LIN)?"linear":"sinc",
					cs.samp?'E':'R',
					cs.chs,cs.chs>1?"s":"",
					cs.trig?cs.slope?'/':'\\':'A');
				XSetForeground(dpy,gc,0xffffff);
				XDrawString(dpy,pm,gc,0,ph-1,str,strlen(str));
				// send itself an exposure event
				// to display the oscillogram
				evt.type=Expose;
				XSendEvent(dpy,win,False,0,&evt);
				// freeze if we're in single sweep mode
				if (mode&O_SNGL) {
					mode&=~O_RUN;
					// remove device fd
					// from the poll structure
					// to ignore serial events
					pfds[0].fd=-1;
					// change window name
					XStoreName(dpy,win,\
					"ascope [frozen]");
					// quit single sweep mode as well
					mode&=~O_SNGL;
				}
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
				// clear update CW flag
				sendcw=0;
				if (ks==XK_q) {
					// quit
					return 0;
				}
				if (sync && mode&O_RUN && ks==XK_m) {
					// toggle sampling mode
					cs.samp=(cs.samp==1)?0:1;
					sendcw=1;
				}
				if (sync && mode&O_RUN && isdigit(str[0])) {
					// set number of channels
					str[1]=0;
					cs.chs=atoi(str);
					if (cs.chs<1) cs.chs=1;
					if (cs.chs>MAXCHS) cs.chs=MAXCHS;
					sendcw=1;
				}
				if (sync && mode&O_RUN && ks==XK_plus) {
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
				if (sync && mode&O_RUN && ks==XK_minus) {
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
				if (sync && cs.samp==0 && mode&O_RUN && ks==XK_a) {
					// set auto-trigger mode
					cs.trig=0;
					sendcw=1;
				}
				if (sync && mode&O_RUN && ks==XK_slash) {
					// trigger on rising edge
					cs.slope=1;
					cs.trig=1;
					sendcw=1;
				}
				if (sync && mode&O_RUN && ks==XK_backslash) {
					// trigger on falling edge
					cs.slope=0;
					cs.trig=1;
					sendcw=1;
				}
				if (sync && mode&O_RUN && ks==XK_Right) {
					// increase time zoom
					if (p<MAXP) {
						++p;
						zt=1<<p;
					}
				}
				if (sync && mode&O_RUN && ks==XK_Left) {
					// decrease time zoom
					if (p>0) {
						--p;
						zt=1<<p;
					}
				}
				if (sync && mode&O_RUN && ks==XK_Up) {
					// increase voltage zoom
					++zv;
				}
				if (sync && mode&O_RUN && ks==XK_Down) {
					// decrease voltage zoom
					if (zv>1)
						--zv;
				}
				if (sync && mode&O_RUN && p && ks==XK_i) {
					// toggle interpolation mode
					mode^=O_LIN;
				}
				if (ks==XK_space) {
					// toggle run mode
					mode^=O_RUN;
					if (mode&O_RUN) {
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
				if (sync && cs.samp==0 && ks==XK_s) {
					// enter single sweep mode
					mode|=O_SNGL;
					// change window name
					XStoreName(dpy,win,"ascope [single-sweep]");
				}
				if (rdy && ks==XK_d) {
					// dump raw buffer to stderr
					for (ch=0; ch<cs.chs; ++ch)
						for (n=0; n<N; ++n)
							fprintf(stderr,\
							"%hhu\n",rbuf[ch][n]);
					fflush(stderr);
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
					// open file
					of=fopen(ofname,"w");
					if (of==NULL) {
						fprintf(stderr,"Can't open %s: %s\n",ofname,strerror(errno));
					} else {
						png_image_write_to_stdio(&pimg,of,0,buf,0,NULL);
						fclose(of);
						printf("wrote %s\n",ofname);
					}
					// cleanup
					free(buf);
					XDestroyImage(ximg);
				}
				// update and send the new CW
				if (sendcw) {
					// make control word
					cw=makecw(cs);
					// send it to the device
					write(fd,&cw,1);
					tcflush(fd,TCOFLUSH);
				}
			}
			if (sync && evt.type==ButtonPress) {
				// show time and voltage below mouse pointer
				float x,y;
				x=evt.xbutton.x-B;
				y=evt.xbutton.y-B;
				if (x<W && y<H) {
					float t,v;
					t=(x/W)*N*dt(cs)/zt;
					v=(Vmax-(Vmax-Vmin)*y/(H-1))/zv;
					printf("%.1f us, %.2f V\n",t,v);
				}
			}
		}
	}
}
