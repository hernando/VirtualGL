/* Copyright (C)2004 Landmark Graphics
 *
 * This library is free software and may be redistributed and/or modified under
 * the terms of the wxWindows Library License, Version 3 or (at your option)
 * any later version.  The full license is in the LICENSE.txt file included
 * with this distribution.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * wxWindows Library License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/signal.h>
#include <string.h>
#include "rrutil.h"
#include "rrtimer.h"
#include "rrthread.h"
#include "rrmutex.h"
#include "fakerconfig.h"

FakerConfig fconfig;

#include "faker-dpyhash.h"
#include "faker-winhash.h"
#include "faker-ctxhash.h"
#include "faker-vishash.h"
#include "faker-cfghash.h"
#include "faker-pmhash.h"
#include "faker-glxdhash.h"
#include "faker-sym.h"
#include "glxvisual.h"
#include <sys/types.h>
#include <unistd.h>
#ifdef __DEBUG__
#include "x11err.h"
#endif

#ifdef SUNOGL
extern "C" {
static GLvoid r_glIndexd(OglContextPtr, GLdouble);
static GLvoid r_glIndexf(OglContextPtr, GLfloat);
static GLvoid r_glIndexi(OglContextPtr, GLint);
static GLvoid r_glIndexs(OglContextPtr, GLshort);
static GLvoid r_glIndexub(OglContextPtr, GLubyte);
static GLvoid r_glIndexdv(OglContextPtr, const GLdouble *);
static GLvoid r_glIndexfv(OglContextPtr, const GLfloat *);
static GLvoid r_glIndexiv(OglContextPtr, const GLint *);
static GLvoid r_glIndexsv(OglContextPtr, const GLshort *);
static GLvoid r_glIndexubv(OglContextPtr, const GLubyte *);
}
#endif

// Did I mention that debugging real-time systems is hell?
void _vglprintf (FILE *f, const char *format, ...)
{
	static rrcs mutex;  static rrtimer timer;
	rrcs::safelock l(mutex);
	va_list arglist;
	va_start(arglist, format);
	fprintf(f, "T0x%.8lx %.6f C0x%.8lx D0x%.8lx R0x%.8lx\n - ", (unsigned long)rrthread_id(), timer.time(),
		(unsigned long)glXGetCurrentContext(), GetCurrentDrawable(), GetCurrentReadDrawable());
	vfprintf(f, format, arglist);
	fflush(f);
	va_end(arglist);
}

// Globals
Display *_localdpy=NULL;
#ifdef USEGLP
GLPDevice _localdev=-1;
#define _localdisplayiscurrent() ((fconfig.glp && glPGetCurrentDevice()==_localdev) || (!fconfig.glp && GetCurrentDisplay()==_localdpy))
#else
#define _localdisplayiscurrent() (GetCurrentDisplay()==_localdpy)
#endif
#define _isremote(dpy) (fconfig.glp || (_localdpy && dpy!=_localdpy))
#define _isfront(drawbuf) (drawbuf==GL_FRONT || drawbuf==GL_FRONT_AND_BACK \
	|| drawbuf==GL_FRONT_LEFT || drawbuf==GL_FRONT_RIGHT || drawbuf==GL_LEFT \
	|| drawbuf==GL_RIGHT)
#define _isright(drawbuf) (drawbuf==GL_RIGHT || drawbuf==GL_FRONT_RIGHT \
	|| drawbuf==GL_BACK_RIGHT)

static inline int _drawingtofront(void)
{
	GLint drawbuf=GL_BACK;
	glGetIntegerv(GL_DRAW_BUFFER, &drawbuf);
	return _isfront(drawbuf);
}

static inline int _drawingtoright(void)
{
	GLint drawbuf=GL_LEFT;
	glGetIntegerv(GL_DRAW_BUFFER, &drawbuf);
	return _isright(drawbuf);
}

static rrcs globalmutex;
winhash *_winh=NULL;  dpyhash *_dpyh=NULL;  ctxhash ctxh;  vishash vish;  cfghash cfgh;  pmhash pmh;  glxdhash glxdh;
#define dpyh (*(_dpyh?_dpyh:(_dpyh=new dpyhash())))
#define winh (*(_winh?_winh:(_winh=new winhash())))

static int __shutdown=0;

static inline int isdead(void)
{
	int retval=0;
	rrcs::safelock l(globalmutex);
	retval=__shutdown;
	return retval;
}

void __vgl_safeexit(int retcode)
{
	int shutdown;
	globalmutex.lock(false);
	shutdown=__shutdown;
	if(!__shutdown)
	{
		__shutdown=1;
		pmh.killhash();
		vish.killhash();
		cfgh.killhash();
		ctxh.killhash();
		glxdh.killhash();
		if(_dpyh) _dpyh->killhash();
		if(_winh) _winh->killhash();
	}
	globalmutex.unlock(false);
	if(!shutdown) exit(retcode);
	else pthread_exit(0);
}

#define _die(f,m) {if(!isdead()) fprintf(stderr, "[VGL] %s--\n[VGL] %s\n", f, m);  __vgl_safeexit(1);}

#define TRY() try {
#define CATCH() } catch(rrerror &e) {_die(e.getMethod(), e.getMessage());}

#define prargs(a) fprintf(stderr, "%s=%s ", #a, a?a:"NULL")
#define prargx(a) fprintf(stderr, "%s=0x%.8lx ", #a, (unsigned long)a)
#define prargi(a) fprintf(stderr, "%s=%d ", #a, a)
#define prargv(a) fprintf(stderr, "%s=0x%.8lx(0x%.2lx) ", #a, (unsigned long)a, a?a->visualid:0)
#define prargc(a) fprintf(stderr, "%s=0x%.8lx(0x%.2x) ", #a, (unsigned long)a, a?_FBCID(a):0)
#define prargal11(a) if(a) {  \
	fprintf(stderr, "attrib_list=[");  \
	for(int __an=0; attrib_list[__an]!=None; __an++) {  \
		fprintf(stderr, "0x%.4x", attrib_list[__an]);  \
		if(attrib_list[__an]!=GLX_USE_GL && attrib_list[__an]!=GLX_DOUBLEBUFFER  \
			&& attrib_list[__an]!=GLX_STEREO && attrib_list[__an]!=GLX_RGBA)  \
			fprintf(stderr, "=0x%.4x", attrib_list[++__an]);  \
		fprintf(stderr, " ");  \
	}  fprintf(stderr, "] ");}
#define prargal13(a) if(a) {  \
	fprintf(stderr, "attrib_list=[");  \
	for(int __an=0; attrib_list[__an]!=None; __an+=2) {  \
		fprintf(stderr, "0x%.4x=0x%.4x ", attrib_list[__an], attrib_list[__an+1]);  \
	}  fprintf(stderr, "] ");}

#define opentrace(f)  \
	double __vgltracetime=0.;  \
	if(fconfig.trace) {  \
		fprintf(stderr, "[VGL] %s (", #f);  \

#define starttrace()  \
		__vgltracetime=rrtime();  \
	}

#define stoptrace()  \
	if(fconfig.trace) {  \
		__vgltracetime=rrtime()-__vgltracetime;

#define closetrace()  \
		fprintf(stderr, ") %f ms\n", __vgltracetime*1000.);  \
		fflush(stderr);  \
	}

#include "faker-glx.cpp"

#if 0
// Used during debug so we can get a stack trace from an X11 protocol error
#ifdef __DEBUG__
int xhandler(Display *dpy, XErrorEvent *xe)
{
	fprintf(stderr, "[VGL] X11 Error--\n[VGL] %s\n", x11error(xe->error_code));  fflush(stderr);
	return 0;
}
#endif
#endif

static void fakerinit(void)
{
	static int init=0;

	rrcs::safelock l(globalmutex);
	if(init) return;
	init=1;

	fconfig.reloadenv();

	if(!_dpyh) errifnot(_dpyh=new dpyhash());
	if(!_winh) errifnot(_winh=new winhash())
	#ifdef __DEBUG__
	if(getenv("VGL_DEBUG"))
	{
		printf("[VGL] Attach debugger to process %d ...\n", getpid());
		fgetc(stdin);
	}
	#if 0
	XSetErrorHandler(xhandler);
	#endif
	#endif

	__vgl_loadsymbols();
	#ifdef USEGLP
	if(fconfig.glp)
	{
		if(_localdev<0)
		{
			char **devices=NULL;  int ndevices=0;  char *device=NULL;
			if((devices=glPGetDeviceNames(&ndevices))==NULL || ndevices<1)
				_throw("No GLP devices are registered");
			device=fconfig.localdpystring;
			if(!strnicmp(device, "GLP", 3)) device=NULL;
			if((_localdev=glPOpenDevice(device))<0)
			{
				fprintf(stderr, "[VGL] Could not open device %s.\n", fconfig.localdpystring);
				__vgl_safeexit(1);
			}
		}
	}
	else
	#endif
	if(!_localdpy)
	{
		if((_localdpy=_XOpenDisplay(fconfig.localdpystring))==NULL)
		{
			fprintf(stderr, "[VGL] Could not open display %s.\n", fconfig.localdpystring);
			__vgl_safeexit(1);
		}
	}
}

////////////////
// X11 functions
////////////////

extern "C" {

Display *XOpenDisplay(_Xconst char* name)
{
	Display *dpy=NULL;
	TRY();

		opentrace(XOpenDisplay);  prargs(name);  starttrace();

	fakerinit();
	if(!(dpy=_XOpenDisplay(name))) return NULL;
	dpyh.add(dpy);

		stoptrace();  prargx(dpy);  closetrace();

	CATCH();
	return dpy;
}

int XCloseDisplay(Display *dpy)
{
	int retval=0;
	TRY();

		opentrace(XCloseDisplay);  prargx(dpy);  starttrace();

	dpyh.remove(dpy);
	retval=_XCloseDisplay(dpy);

		stoptrace();  closetrace();

	CATCH();
	return retval;
}

Window XCreateWindow(Display *dpy, Window parent, int x, int y,
	unsigned int width, unsigned int height, unsigned int border_width,
	int depth, unsigned int c_class, Visual *visual, unsigned long valuemask,
	XSetWindowAttributes *attributes)
{
	Window win=0;
	TRY();

		opentrace(XCreateWindow);  prargx(dpy);  prargx(parent);  prargi(x);
		prargi(y);  prargi(width);  prargi(height);  prargi(depth);
		prargi(c_class);  prargv(visual);  starttrace();

	if(!(win=_XCreateWindow(dpy, parent, x, y, width, height, border_width,
		depth, c_class, visual, valuemask, attributes))) return 0;
	if(_isremote(dpy)) winh.add(dpy, win);

		stoptrace();  prargx(win);  closetrace();

	CATCH();
	return win;
}

Window XCreateSimpleWindow(Display *dpy, Window parent, int x, int y,
	unsigned int width, unsigned int height, unsigned int border_width,
	unsigned long border, unsigned long background)
{
	Window win=0;
	TRY();

		opentrace(XCreateSimpleWindow);  prargx(dpy);  prargx(parent);  prargi(x);
		prargi(y);  prargi(width);  prargi(height);  starttrace();

	if(!(win=_XCreateSimpleWindow(dpy, parent, x, y, width, height, border_width,
		border, background))) return 0;
	if(_isremote(dpy)) winh.add(dpy, win);

		stoptrace();  prargx(win);  closetrace();

	CATCH();
	return win;
}

int XDestroyWindow(Display *dpy, Window win)
{
	int retval=0;
	TRY();

		opentrace(XDestroyWindow);  prargx(dpy);  prargx(win);  starttrace();

	winh.remove(dpy, win);
	retval=_XDestroyWindow(dpy, win);

		stoptrace();  closetrace();

	CATCH();
	return retval;
}

#if 0
Window FindTopLevelWindow(Display *dpy, Window win)
{
	Window w=win, root, parent, *children;  unsigned int nchildren;
	for(;;)
	{
		XQueryTree(dpy, w, &root, &parent, &children, &nchildren);
		if(parent==root) break;
		else w=parent;
	}
	return w;
}

// Walk the window tree, notifying any GL windows belonging to the same TLW
void SetQualRecursive(Display *dpy, Window start, int qual, int subsamp, bool readback)
{
	Window root, parent, *children;  unsigned int nchildren;
	unsigned int i;  pbwin *pbw=NULL;
	pbw=winh.findpb(dpy, start);
	if(pbw)
	{
		_vglprintf(stdout, "SQR: Setting qual\n");
		pbw->setqual(qual, subsamp, readback);
	}
	XQueryTree(dpy, start, &root, &parent, &children, &nchildren);
	if(nchildren==0) return;
	for(i=0; i<nchildren; i++) SetQualRecursive(dpy, children[i], qual, subsamp, readback);
}
#endif

static void _HandleEvent(Display *dpy, XEvent *xe)
{
	pbwin *pbw=NULL;
	if(xe && xe->type==ConfigureNotify)
	{
		pbw=winh.findpb(dpy, xe->xconfigure.window);
		if(pbw)
		{
				opentrace(_HandleEvent);  prargi(xe->xconfigure.width);
				prargi(xe->xconfigure.height);  starttrace();

			pbw->resize(xe->xconfigure.width, xe->xconfigure.height);

				stoptrace();  closetrace();
		}
	}
	#if 0
	else if(xe && xe->type==ButtonPress)
	{
		fconfig.setloqual();
//		Window start=FindTopLevelWindow(dpy, xe->xbutton.window);
//		SetQualRecursive(dpy, start, fconfig.loqual, fconfig.losubsamp, false);
	}
	else if(xe && xe->type==ButtonRelease)
	{
		fconfig.sethiqual();
//		Window start=FindTopLevelWindow(dpy, xe->xbutton.window);
//		SetQualRecursive(dpy, start, fconfig.hiqual, fconfig.hisubsamp, false);
	}
	#endif
}

int XNextEvent(Display *dpy, XEvent *xe)
{
	int retval=0;
	TRY();
	retval=_XNextEvent(dpy, xe);
	_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

int XWindowEvent(Display *dpy, Window win, long event_mask, XEvent *xe)
{
	int retval=0;
	TRY();
	retval=_XWindowEvent(dpy, win, event_mask, xe);
	_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

Bool XCheckWindowEvent(Display *dpy, Window win, long event_mask, XEvent *xe)
{
	Bool retval=0;
	TRY();
	if((retval=_XCheckWindowEvent(dpy, win, event_mask, xe))==True)
		_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

int XMaskEvent(Display *dpy, long event_mask, XEvent *xe)
{
	int retval=0;
	TRY();
	retval=_XMaskEvent(dpy, event_mask, xe);
	_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

Bool XCheckMaskEvent(Display *dpy, long event_mask, XEvent *xe)
{
	Bool retval=0;
	TRY();
	if((retval=_XCheckMaskEvent(dpy, event_mask, xe))==True)
		_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

Bool XCheckTypedEvent(Display *dpy, int event_type, XEvent *xe)
{
	Bool retval=0;
	TRY();
	if((retval=_XCheckTypedEvent(dpy, event_type, xe))==True)
		_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

Bool XCheckTypedWindowEvent(Display *dpy, Window win, int event_type, XEvent *xe)
{
	Bool retval=0;
	TRY();
	if((retval=_XCheckTypedWindowEvent(dpy, win, event_type, xe))==True)
		_HandleEvent(dpy, xe);
	CATCH();
	return retval;
}

int XConfigureWindow(Display *dpy, Window win, unsigned int value_mask, XWindowChanges *values)
{
	int retval=0;
	TRY();

		opentrace(XConfigureWindow);  prargx(dpy);  prargx(win);
		if(values && (value_mask&CWWidth)) {prargi(values->width);}
		if(values && (value_mask&CWHeight)) {prargi(values->height);}  starttrace();

	pbwin *pbw=NULL;
	pbw=winh.findpb(dpy, win);
	if(pbw && values)
		pbw->resize(value_mask&CWWidth?values->width:0, value_mask&CWHeight?values->height:0);
	retval=_XConfigureWindow(dpy, win, value_mask, values);

		stoptrace();  closetrace();

	CATCH();
	return retval;
}

int XResizeWindow(Display *dpy, Window win, unsigned int width, unsigned int height)
{
	int retval=0;
	TRY();

		opentrace(XResizeWindow);  prargx(dpy);  prargx(win);  prargi(width);
		prargi(height);  starttrace();

	pbwin *pbw=NULL;
	pbw=winh.findpb(dpy, win);
	if(pbw) pbw->resize(width, height);
	retval=_XResizeWindow(dpy, win, width, height);

		stoptrace();  closetrace();

	CATCH();
	return retval;
}

int XMoveResizeWindow(Display *dpy, Window win, int x, int y, unsigned int width, unsigned int height)
{
	int retval=0;
	TRY();

		opentrace(XMoveResizeWindow);  prargx(dpy);  prargx(win);  prargi(x);
		prargi(y);  prargi(width);  prargi(height);  starttrace();

	pbwin *pbw=NULL;
	pbw=winh.findpb(dpy, win);
	if(pbw) pbw->resize(width, height);
	retval=_XMoveResizeWindow(dpy, win, x, y, width, height);

		stoptrace();  closetrace();

	CATCH();
	return retval;
}

// We have to trap any attempts to copy from/to a GLXPixmap (ugh)
// It should work as long as a valid GLX context is current
// in the calling thread
int XCopyArea(Display *dpy, Drawable src, Drawable dst, GC gc, int src_x, int src_y,
	unsigned int w, unsigned int h, int dest_x, int dest_y)
{
	TRY();
	pbuffer *pb;
	GLXDrawable read=src, draw=dst;  bool srcpm=false, dstpm=false;
	if((pb=pmh.find(dpy, src))!=0) {read=pb->drawable();  srcpm=true;}
	if((pb=pmh.find(dpy, dst))!=0) {draw=pb->drawable();  dstpm=true;}
	if(!srcpm && !dstpm) return _XCopyArea(dpy, src, dst, gc, src_x, src_y, w, h, dest_x, dest_y);

		opentrace(XCopyArea);  prargx(dpy);  prargx(src);  prargx(dst);  prargx(gc);
		prargi(src_x);  prargi(src_y);  prargi(w);  prargi(h);  prargi(dest_x);
		prargi(dest_y);  prargx(read);  prargx(draw);  starttrace();

	GLXDrawable oldread=GetCurrentReadDrawable();
	GLXDrawable olddraw=GetCurrentDrawable();
	GLXContext ctx=glXGetCurrentContext();
	Display *olddpy=NULL;
	if(!ctx || (!fconfig.glp && !(olddpy=GetCurrentDisplay())))
		return 0;  // Does ... not ... compute

	// Intentionally call the faked function so it will map a PB if src or dst is a window
	glXMakeContextCurrent(dpy, draw, read, ctx);

	unsigned int srch, dsth, dstw;
	#ifdef USEGLP
	if(fconfig.glp)
	{
		glPQueryBuffer(glPGetCurrentBuffer(), GLP_WIDTH, &dstw);
		glPQueryBuffer(glPGetCurrentBuffer(), GLP_HEIGHT, &dsth);
		glPQueryBuffer(glPGetCurrentReadBuffer(), GLP_HEIGHT, &srch);
	}
	else
	#endif
	{
		_glXQueryDrawable(_glXGetCurrentDisplay(), _glXGetCurrentDrawable(), GLX_WIDTH, &dstw);
		_glXQueryDrawable(_glXGetCurrentDisplay(), _glXGetCurrentDrawable(), GLX_HEIGHT, &dsth);
		_glXQueryDrawable(_glXGetCurrentDisplay(), _glXGetCurrentReadDrawable(), GLX_HEIGHT, &srch);
	}

	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
	glPushAttrib(GL_VIEWPORT_BIT|GL_COLOR_BUFFER_BIT|GL_PIXEL_MODE_BIT);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	_glViewport(0, 0, dstw, dsth);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, dstw, 0, dsth, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	_glDrawBuffer(GL_FRONT_AND_BACK);
	glReadBuffer(GL_FRONT);
	for(unsigned int i=0; i<h; i++)
	{
		glRasterPos2i(dest_x, dsth-dest_y-i-1);
		glCopyPixels(src_x, srch-src_y-i-1, w, 1, GL_COLOR);
	}
	glFlush();  // call faked function here, so it will perform a readback

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	_glPopAttrib();
	glPopClientAttrib();

	#ifdef USEGLP
	if(fconfig.glp) glPMakeContextCurrent(olddraw, oldread, ctx);
	else
	#endif
	_glXMakeContextCurrent(olddpy, olddraw, oldread, ctx);

		stoptrace();  closetrace();

	CATCH();
	return 0;
}

int XFree(void *data)
{
	int ret=0;
	TRY();
	ret=_XFree(data);
	if(data) vish.remove(NULL, (XVisualInfo *)data);
	CATCH();
	return ret;
}

/////////////////////////////
// GLX 1.0 Context management
/////////////////////////////

XVisualInfo *glXChooseVisual(Display *dpy, int screen, int *attrib_list)
{
	XVisualInfo *v=NULL;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXChooseVisual(dpy, screen, attrib_list);
	////////////////////

		opentrace(glXChooseVisual);  prargx(dpy);  prargi(screen);
		prargal11(attrib_list);  starttrace();

	GLXFBConfig *configs=NULL, c=0;  int n=0;
	if(!dpy || !attrib_list) return NULL;
	int depth=24, c_class=TrueColor, level=0, stereo=0, trans=0;
	if(!(configs=__vglConfigsFromVisAttribs(attrib_list, screen, depth, c_class,
		level, stereo, trans, n)) || n<1) return NULL;
	c=configs[0];
	XFree(configs);
	VisualID vid=__vglMatchVisual(dpy, screen, depth, c_class, level, stereo, trans);
	if(!vid) return NULL;
	v=__vglVisualFromVisualID(dpy, vid);
	if(!v) return NULL;
	vish.add(dpy, v, c);

		stoptrace();  prargv(v);  prargc(c);  closetrace();

	CATCH();
	return v;
}

XVisualInfo *glXGetVisualFromFBConfig(Display *dpy, GLXFBConfig config)
{
	XVisualInfo *v=NULL;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXGetVisualFromFBConfig(dpy, config);
	////////////////////

		opentrace(glXGetVisualFromFBConfig);  prargx(dpy);  prargc(config);
		starttrace();

	VisualID vid=0;
	if(!dpy || !config) return NULL;
	vid=_MatchVisual(dpy, config);
	if(!vid) return NULL;
	v=__vglVisualFromVisualID(dpy, vid);
	if(!v) return NULL;
	vish.add(dpy, v, config);

		stoptrace();  prargv(v);  closetrace();

	CATCH();
	return v;
}

XVisualInfo *glXGetVisualFromFBConfigSGIX(Display *dpy, GLXFBConfigSGIX config)
{
	return glXGetVisualFromFBConfig(dpy, config);
}

GLXContext glXCreateContext(Display *dpy, XVisualInfo *vis, GLXContext share_list, Bool direct)
{
	GLXContext ctx=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXCreateContext(dpy, vis, share_list, direct);
	////////////////////

		opentrace(glXCreateContext);  prargx(dpy);  prargv(vis);
		prargi(direct);  starttrace();

	GLXFBConfig c;
	if(!(c=_MatchConfig(dpy, vis))) _throw("Could not obtain Pbuffer visual");
	int render_type=__vglServerVisualAttrib(c, GLX_RENDER_TYPE);
	#ifdef USEGLP
	if(fconfig.glp)
	{
		if(!(ctx=glPCreateNewContext(c,
			render_type==GLP_COLOR_INDEX_BIT?GLP_COLOR_INDEX_TYPE:GLP_RGBA_TYPE, share_list)))
		return NULL;
	}
	else
	#endif
	{
		if(!(ctx=_glXCreateNewContext(_localdpy, c,
			render_type==GLX_COLOR_INDEX_BIT?GLX_COLOR_INDEX_TYPE:GLX_RGBA_TYPE, share_list, True)))
			return NULL;
	}
	ctxh.add(ctx, c);

		stoptrace();  prargc(c);  prargx(ctx);  closetrace();

	CATCH();
	return ctx;
}

Bool glXMakeCurrent(Display *dpy, GLXDrawable drawable, GLXContext ctx)
{
	Bool retval=0;
	TRY();
	pbwin *pbw;  GLXFBConfig config=0;

	// Prevent recursion
	if(!_isremote(dpy)) return _glXMakeCurrent(dpy, drawable, ctx);
	////////////////////

		opentrace(glXMakeCurrent);  prargx(dpy);  prargx(drawable);  prargx(ctx);
		starttrace();

	// Equivalent of a glFlush()
	GLXDrawable curdraw=GetCurrentDrawable();
	if(glXGetCurrentContext() && _localdisplayiscurrent()
	&& curdraw && (pbw=winh.findpb(curdraw))!=NULL)
	{
		pbwin *newpbw;
		if(drawable==0 || (newpbw=winh.findpb(dpy, drawable))==NULL
		|| newpbw->getdrawable()!=curdraw)
		{
			if(_drawingtofront() || pbw->_dirty) pbw->readback(GL_FRONT, true);
		}
	}

	// If the drawable isn't a window, we pass it through unmodified, else we
	// map it to a Pbuffer
	if(dpy && drawable && ctx)
	{
		errifnot(config=ctxh.findconfig(ctx));
		pbw=winh.setpb(dpy, drawable, config);
		if(pbw)
		{
			drawable=pbw->updatedrawable();
			if(drawable!=curdraw) pbw->forcenextframe();
		}
	}

	#ifdef USEGLP
	if(fconfig.glp)
		retval=glPMakeContextCurrent(drawable, drawable, ctx);
	else
	#endif
		retval=_glXMakeContextCurrent(_localdpy, drawable, drawable, ctx);
	if((pbw=winh.findpb(drawable))!=NULL) {pbw->clear();  pbw->cleanup();}
	pbuffer *pb;
	if((pb=pmh.find(dpy, drawable))!=NULL) pb->clear();
	#ifdef SUNOGL
	sunOglCurPrimTablePtr->oglIndexd=r_glIndexd;
	sunOglCurPrimTablePtr->oglIndexf=r_glIndexf;
	sunOglCurPrimTablePtr->oglIndexi=r_glIndexi;
	sunOglCurPrimTablePtr->oglIndexs=r_glIndexs;
	sunOglCurPrimTablePtr->oglIndexub=r_glIndexub;
	sunOglCurPrimTablePtr->oglIndexdv=r_glIndexdv;
	sunOglCurPrimTablePtr->oglIndexfv=r_glIndexfv;
	sunOglCurPrimTablePtr->oglIndexiv=r_glIndexiv;
	sunOglCurPrimTablePtr->oglIndexsv=r_glIndexsv;
	sunOglCurPrimTablePtr->oglIndexubv=r_glIndexubv;
	#endif

		stoptrace();  prargc(config);  prargx(drawable);  closetrace();

	CATCH();
	return retval;
}

void glXDestroyContext(Display* dpy, GLXContext ctx)
{
	TRY();

		opentrace(glXDestroyContext);  prargx(dpy);  prargx(ctx);  starttrace();

	ctxh.remove(ctx);
	#ifdef USEGLP
	if(fconfig.glp)	glPDestroyContext(ctx);
	else
	#endif
	_glXDestroyContext(_localdpy, ctx);

		stoptrace();  closetrace();

	CATCH();
}

/////////////////////////////
// GLX 1.3 Context management
/////////////////////////////

GLXContext glXCreateNewContext(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, Bool direct)
{
	GLXContext ctx=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXCreateNewContext(dpy, config, render_type, share_list, direct);
	////////////////////

		opentrace(glXCreateNewContext);  prargx(dpy);  prargc(config);
		prargi(render_type);  prargi(direct);  starttrace();

	#ifdef USEGLP
	if(fconfig.glp)
	{
		if(!(ctx=glPCreateNewContext(config, render_type, share_list)))
			return NULL;
	}
	else
	#endif
	{
		if(!(ctx=_glXCreateNewContext(_localdpy, config, render_type, share_list, True)))
			return NULL;
	}
	ctxh.add(ctx, config);

		stoptrace();  prargx(ctx);  closetrace();

	CATCH();
	return ctx;
}

Bool glXMakeContextCurrent(Display *dpy, GLXDrawable draw, GLXDrawable read, GLXContext ctx)
{
	Bool retval=0;
	pbwin *pbw;  GLXFBConfig config=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXMakeContextCurrent(dpy, draw, read, ctx);
	////////////////////

		opentrace(glXMakeContextCurrent);  prargx(dpy);  prargx(draw);
		prargx(read);  prargx(ctx);  starttrace();

	// Equivalent of a glFlush()
	GLXDrawable curdraw=GetCurrentDrawable();
	if(glXGetCurrentContext() && _localdisplayiscurrent()
	&& curdraw && (pbw=winh.findpb(curdraw))!=NULL)
	{
		pbwin *newpbw;
		if(draw==0 || (newpbw=winh.findpb(dpy, draw))==NULL
			|| newpbw->getdrawable()!=curdraw)
		{
			if(_drawingtofront() || pbw->_dirty) pbw->readback(GL_FRONT, true);
		}
	}

	// If the drawable isn't a window, we pass it through unmodified, else we
	// map it to a Pbuffer
	pbwin *drawpbw, *readpbw;
	if(dpy && (draw || read) && ctx)
	{
		errifnot(config=ctxh.findconfig(ctx));
		drawpbw=winh.setpb(dpy, draw, config);
		readpbw=winh.setpb(dpy, read, config);
		if(drawpbw)
		{
			draw=drawpbw->updatedrawable();
			if(draw!=curdraw) drawpbw->forcenextframe();
		}
		if(readpbw) read=readpbw->updatedrawable();
	}
	#ifdef USEGLP
	if(fconfig.glp)
		retval=glPMakeContextCurrent(draw, read, ctx);
	else
	#endif
		retval=_glXMakeContextCurrent(_localdpy, draw, read, ctx);
	if((drawpbw=winh.findpb(draw))!=NULL) {drawpbw->clear();  drawpbw->cleanup();}
	if((readpbw=winh.findpb(read))!=NULL) readpbw->cleanup();
	pbuffer *pb;
	if((pb=pmh.find(dpy, draw))!=NULL) pb->clear();
	#ifdef SUNOGL
	sunOglCurPrimTablePtr->oglIndexd=r_glIndexd;
	sunOglCurPrimTablePtr->oglIndexf=r_glIndexf;
	sunOglCurPrimTablePtr->oglIndexi=r_glIndexi;
	sunOglCurPrimTablePtr->oglIndexs=r_glIndexs;
	sunOglCurPrimTablePtr->oglIndexub=r_glIndexub;
	sunOglCurPrimTablePtr->oglIndexdv=r_glIndexdv;
	sunOglCurPrimTablePtr->oglIndexfv=r_glIndexfv;
	sunOglCurPrimTablePtr->oglIndexiv=r_glIndexiv;
	sunOglCurPrimTablePtr->oglIndexsv=r_glIndexsv;
	sunOglCurPrimTablePtr->oglIndexubv=r_glIndexubv;
	#endif

		stoptrace();  prargc(config);  prargx(draw);  prargx(read);  closetrace();

	CATCH();
	return retval;
}

Bool glXMakeCurrentReadSGI(Display *dpy, GLXDrawable draw, GLXDrawable read, GLXContext ctx)
{
	return glXMakeContextCurrent(dpy, draw, read, ctx);
}

///////////////////////////////////
// SGIX_fbconfig Context management
///////////////////////////////////

// On Linux, GLXFBConfigSGIX is typedef'd to GLXFBConfig
GLXContext glXCreateContextWithConfigSGIX(Display *dpy, GLXFBConfigSGIX config, int render_type, GLXContext share_list, Bool direct)
{
	return glXCreateNewContext(dpy, config, render_type, share_list, direct);
}

/////////////////////////
// Other GL/GLX functions
/////////////////////////

// Here, we fake out the client into thinking it's getting a window drawable,
// but really it's getting a Pbuffer drawable
GLXWindow glXCreateWindow(Display *dpy, GLXFBConfig config, Window win, const int *attrib_list)
{
	TRY();
	pbwin *pbw;
	XSync(dpy, False);

	// Prevent recursion
	if(!_isremote(dpy)) return _glXCreateWindow(dpy, config, win, attrib_list);
	////////////////////

		opentrace(glXCreateWindow);  prargx(dpy);  prargc(config);  prargx(win);
		starttrace();

	errifnot(pbw=winh.setpb(dpy, win, config));

		stoptrace();  if(pbw) {prargx(pbw->getdrawable());}  closetrace();

	CATCH();
	return win;  // Make the client store the original window handle, which we use
               // to find the Pbuffer in the hash
}

void glXDestroyWindow(Display *dpy, GLXWindow win)
{
	TRY();
	// Prevent recursion
	if(!_isremote(dpy)) {_glXDestroyWindow(dpy, win);  return;}
	////////////////////

		opentrace(glXDestroyWindow);  prargx(dpy);  prargx(win);  starttrace();

	winh.remove(dpy, win);

		stoptrace();  closetrace();

	CATCH();
}

// Pixmap rendering, another shameless hack.  What we're really returning is a
// Pbuffer handle

GLXPixmap glXCreateGLXPixmap(Display *dpy, XVisualInfo *vi, Pixmap pm)
{
	GLXPixmap drawable=0;
	TRY();
	GLXFBConfig c;

	// Prevent recursion
	if(!_isremote(dpy)) return _glXCreateGLXPixmap(dpy, vi, pm);
	////////////////////

		opentrace(glXCreateGLXPixmap);  prargx(dpy);  prargv(vi);  prargx(pm);
		starttrace();

	Window root;  int x, y;  unsigned int w, h, bw, d;
	XGetGeometry(dpy, pm, &root, &x, &y, &w, &h, &bw, &d);
	errifnot(c=_MatchConfig(dpy, vi));
	pbuffer *pb=new pbuffer(w, h, c);
	if(pb)
	{
		pmh.add(dpy, pm, pb);
		glxdh.add(pb->drawable(), dpy);
		drawable=pb->drawable();
	}

		stoptrace();  prargi(x);  prargi(y);  prargi(w);  prargi(h);
		prargi(d);  prargc(c);  prargx(drawable);  closetrace();

	CATCH();
	return drawable;
}

void glXDestroyGLXPixmap(Display *dpy, GLXPixmap pix)
{
	TRY();
	// Prevent recursion
	if(!_isremote(dpy)) {_glXDestroyGLXPixmap(dpy, pix);  return;}
	////////////////////

		opentrace(glXDestroyGLXPixmap);  prargx(dpy);  prargx(pix);  starttrace();

	glxdh.remove(pix);
	pmh.remove(dpy, pix);

		stoptrace();  closetrace();

	CATCH();
}

GLXPixmap glXCreatePixmap(Display *dpy, GLXFBConfig config, Pixmap pm, const int *attribs)
{
	GLXPixmap drawable=0;
	TRY();

	// Prevent recursion
	if(!_isremote(dpy)) return _glXCreatePixmap(dpy, config, pm, attribs);
	////////////////////

		opentrace(glXCreatePixmap);  prargx(dpy);  prargc(config);  prargx(pm);
		starttrace();

	Window root;  int x, y;  unsigned int w, h, bw, d;
	XGetGeometry(dpy, pm, &root, &x, &y, &w, &h, &bw, &d);
	pbuffer *pb=new pbuffer(w, h, config);
	if(pb)
	{
		pmh.add(dpy, pm, pb);
		glxdh.add(pb->drawable(), dpy);
		drawable=pb->drawable();
	}

		stoptrace();  prargi(x);  prargi(y);  prargi(w);  prargi(h);
		prargi(d);  prargx(drawable);  closetrace();

	CATCH();
	return drawable;
}

GLXPixmap glXCreateGLXPixmapWithConfigSGIX(Display *dpy, GLXFBConfigSGIX config, Pixmap pixmap)
{
	return glXCreatePixmap(dpy, config, pixmap, NULL);
}

void glXDestroyPixmap(Display *dpy, GLXPixmap pix)
{
	TRY();
	// Prevent recursion
	if(!_isremote(dpy)) {_glXDestroyPixmap(dpy, pix);  return;}
	////////////////////

		opentrace(glXDestroyPixmap);  prargx(dpy);  prargx(pix);  starttrace();

	glxdh.remove(pix);
	pmh.remove(dpy, pix);

		stoptrace();  closetrace();

	CATCH();
}

#include "xfonts.c"

// We use a tweaked out version of the Mesa glXUseXFont()
// implementation.
void glXUseXFont(Font font, int first, int count, int list_base)
{
	TRY();

		opentrace(glXUseXFont);  prargx(font);  prargi(first);  prargi(count);
		prargi(list_base);  starttrace();

	Fake_glXUseXFont(font, first, count, list_base);

		stoptrace();  closetrace();

	return;
	CATCH();
}

void glXSwapBuffers(Display* dpy, GLXDrawable drawable)
{
	TRY();

		opentrace(glXSwapBuffers);  prargx(dpy);  prargx(drawable);  starttrace();

	pbwin *pbw=NULL;
	if(_isremote(dpy) && (pbw=winh.findpb(dpy, drawable))!=NULL)
	{
		pbw->readback(GL_BACK, false);
		pbw->swapbuffers();
	}
	else {if(!fconfig.glp) _glXSwapBuffers(_localdpy, drawable);}

		stoptrace();  if(_isremote(dpy) && pbw) {prargx(pbw->getdrawable());}
		closetrace();  

	CATCH();
}

static void _doGLreadback(bool force, bool sync=false)
{
	pbwin *pbw;
	GLXDrawable drawable;
	drawable=GetCurrentDrawable();
	if(!drawable) return;
	if((pbw=winh.findpb(drawable))!=NULL)
	{
		if(_drawingtofront() || pbw->_dirty)
		{
				opentrace(_doGLreadback);  prargx(pbw->getdrawable());  prargi(sync);
				prargi(force);  starttrace();

			pbw->readback(GL_FRONT, force, sync);

				stoptrace();  closetrace();
		}
	}
}

void glFlush(void)
{
	TRY();

		if(fconfig.trace) fprintf(stderr, "[VGL] glFlush()\n");

	_glFlush();
	_doGLreadback(false);
	CATCH();
}

void glFinish(void)
{
	TRY();

		if(fconfig.trace) fprintf(stderr, "[VGL] glFinish()\n");

	_glFinish();
	if(fconfig.sync) _doGLreadback(true, true);
	else _doGLreadback(false);
	CATCH();
}

void glXWaitGL(void)
{
	TRY();

		if(fconfig.trace) fprintf(stderr, "[VGL] glXWaitGL()\n");

	#ifdef SUNOGL
	_glFinish();  // Sun's glXWaitGL() calls glFinish(), so we do this to avoid 2 readbacks
	#else
	_glXWaitGL();
	#endif
	if(fconfig.sync) _doGLreadback(true, true);
	else _doGLreadback(false);
	CATCH();
}

// If the application switches the draw buffer before calling glFlush(), we
// set a lazy readback trigger
void glDrawBuffer(GLenum mode)
{
	TRY();

		opentrace(glDrawBuffer);  prargx(mode);  starttrace();

	pbwin *pbw=NULL;  int before=-1, after=-1, rbefore=-1, rafter=-1;
	GLXDrawable drawable=GetCurrentDrawable();
	if(drawable && (pbw=winh.findpb(drawable))!=NULL)
	{
		before=_drawingtofront();
		rbefore=_drawingtoright();
		_glDrawBuffer(mode);
		after=_drawingtofront();
		rafter=_drawingtoright();
		if(before && !after) pbw->_dirty=true;
		if(rbefore && !rafter && pbw->stereo()) pbw->_rdirty=true;
	}
	else _glDrawBuffer(mode);

		stoptrace();  if(drawable && pbw) {prargi(pbw->_dirty);
		prargi(pbw->_rdirty);  prargx(pbw->getdrawable());}  closetrace();

	CATCH();
}

// glPopAttrib() can change the draw buffer state as well :/
void glPopAttrib(void)
{
	TRY();

		opentrace(glPopAttrib);  starttrace();

	pbwin *pbw=NULL;  int before=-1, after=-1, rbefore=-1, rafter=-1;
	GLXDrawable drawable=GetCurrentDrawable();
	if(drawable && (pbw=winh.findpb(drawable))!=NULL)
	{
		before=_drawingtofront();
		rbefore=_drawingtoright();
		_glPopAttrib();
		after=_drawingtofront();
		rafter=_drawingtoright();
		if(before && !after) pbw->_dirty=true;
		if(rbefore && !rafter && pbw->stereo()) pbw->_rdirty=true;
	}
	else _glPopAttrib();

		stoptrace();  if(drawable && pbw) {prargi(pbw->_dirty);
		prargi(pbw->_rdirty);  prargx(pbw->getdrawable());}  closetrace();

	CATCH();
}

// Sometimes XNextEvent() is called from a thread other than the
// rendering thread, so we wait until glViewport() is called and
// take that opportunity to resize the Pbuffer
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	TRY();

		opentrace(glViewport);  prargi(x);  prargi(y);  prargi(width);
		prargi(height);  starttrace();

	GLXContext ctx=glXGetCurrentContext();
	GLXDrawable draw=GetCurrentDrawable();
	GLXDrawable read=GetCurrentReadDrawable();
	Display *dpy=NULL;
	if(!fconfig.glp) dpy=GetCurrentDisplay();
	GLXDrawable newread=0, newdraw=0;
	if((dpy || fconfig.glp) && (draw || read) && ctx)
	{
		newread=read, newdraw=draw;
		pbwin *drawpbw=winh.findpb(draw);
		pbwin *readpbw=winh.findpb(read);
		if(drawpbw) newdraw=drawpbw->updatedrawable();
		if(readpbw) newread=readpbw->updatedrawable();
		if(newread!=read || newdraw!=draw)
		{
			#ifdef USEGLP
			if(fconfig.glp) glPMakeContextCurrent(newdraw, newread, ctx);
			else
			#endif
			_glXMakeContextCurrent(dpy, newdraw, newread, ctx);
			if(drawpbw) {drawpbw->clear();  drawpbw->cleanup();}
			if(readpbw) readpbw->cleanup();
		}
	}
	_glViewport(x, y, width, height);

		stoptrace();  if(draw!=newdraw) {prargx(draw);  prargx(newdraw);}
		if(read!=newread) {prargx(read);  prargx(newread);}  closetrace();

	CATCH();
}

// The following nastiness is necessary to make color index rendering work,
// since most platforms don't support color index Pbuffers

/*
glGet GL_CURRENT_INDEX
glIndexPointer
*/

#ifdef SUNOGL

static GLvoid r_glIndexd(OglContextPtr ctx, GLdouble c)
	{glColor3d(c/255., 0.0, 0.0);  return;}
static GLvoid r_glIndexf(OglContextPtr ctx, GLfloat c)
	{glColor3f(c/255., 0., 0.);  return;}
static GLvoid r_glIndexi(OglContextPtr ctx, GLint c)
	{glColor3f((GLfloat)c/255., 0, 0);  return;}
static GLvoid r_glIndexs(OglContextPtr ctx, GLshort c)
	{glColor3f((GLfloat)c/255., 0, 0);  return;}
static GLvoid r_glIndexub(OglContextPtr ctx, GLubyte c)
	{glColor3f((GLfloat)c/255., 0, 0);  return;}

static GLvoid r_glIndexdv(OglContextPtr ctx, const GLdouble *c)
{
	GLdouble v[3]={c? (*c)/255.:0., 0., 0.};
	glColor3dv(c? v:NULL);  return;
}

static GLvoid r_glIndexfv(OglContextPtr ctx, const GLfloat *c)
{
	GLfloat v[3]={c? (*c)/255.:0., 0., 0.};
	glColor3fv(c? v:NULL);  return;
}

static GLvoid r_glIndexiv(OglContextPtr ctx, const GLint *c)
{
	GLfloat v[3]={c? (GLfloat)(*c)/255.:0., 0., 0.};
	glColor3fv(c? v:NULL);  return;
}

static GLvoid r_glIndexsv(OglContextPtr ctx, const GLshort *c)
{
	GLfloat v[3]={c? (GLfloat)(*c)/255.:0., 0., 0.};
	glColor3fv(c? v:NULL);  return;
}

static GLvoid r_glIndexubv(OglContextPtr ctx, const GLubyte *c)
{
	GLfloat v[3]={c? (GLfloat)(*c)/255.:0., 0., 0.};
	glColor3fv(c? v:NULL);  return;
}

void glBegin(GLenum mode)
{
	_glBegin(mode);
	sunOglCurPrimTablePtr->oglIndexd=r_glIndexd;
	sunOglCurPrimTablePtr->oglIndexf=r_glIndexf;
	sunOglCurPrimTablePtr->oglIndexi=r_glIndexi;
	sunOglCurPrimTablePtr->oglIndexs=r_glIndexs;
	sunOglCurPrimTablePtr->oglIndexub=r_glIndexub;
	sunOglCurPrimTablePtr->oglIndexdv=r_glIndexdv;
	sunOglCurPrimTablePtr->oglIndexfv=r_glIndexfv;
	sunOglCurPrimTablePtr->oglIndexiv=r_glIndexiv;
	sunOglCurPrimTablePtr->oglIndexsv=r_glIndexsv;
	sunOglCurPrimTablePtr->oglIndexubv=r_glIndexubv;
}

#else

void glIndexd(GLdouble c) {glColor3d(c/255., 0.0, 0.0);  return;}
void glIndexf(GLfloat c) {glColor3f(c/255., 0., 0.);  return;}
void glIndexi(GLint c) {glColor3f((GLfloat)c/255., 0, 0);  return;}
void glIndexs(GLshort c) {glColor3f((GLfloat)c/255., 0, 0);  return;}
void glIndexub(GLubyte c) {glColor3f((GLfloat)c/255., 0, 0);  return;}

void glIndexdv(const GLdouble *c)
{
	GLdouble v[3]={c? (*c)/255.:0., 0., 0.};
	glColor3dv(c? v:NULL);  return;
}

void glIndexfv(const GLfloat *c)
{
	GLfloat v[3]={c? (*c)/255.:0., 0., 0.};
	glColor3fv(c? v:NULL);  return;
}

void glIndexiv(const GLint *c)
{
	GLfloat v[3]={c? (GLfloat)(*c)/255.:0., 0., 0.};
	glColor3fv(c? v:NULL);  return;
}

void glIndexsv(const GLshort *c)
{
	GLfloat v[3]={c? (GLfloat)(*c)/255.:0., 0., 0.};
	glColor3fv(c? v:NULL);  return;
}

void glIndexubv(const GLubyte *c)
{
	GLfloat v[3]={c? (GLfloat)(*c)/255.:0., 0., 0.};
	glColor3fv(c? v:NULL);  return;
}

#endif

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
	GLenum format, GLenum type, GLvoid *pixels)
{
	if(format==GL_COLOR_INDEX) format=GL_RED;
	return _glReadPixels(x, y, width, height, format, type, pixels);
}

void glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type,
	const GLvoid *pixels)
{
	if(format==GL_COLOR_INDEX) format=GL_RED;
	return _glDrawPixels(width, height, format, type, pixels);
}

void glClearIndex(GLfloat c)
{
	glClearColor(c, 0., 0., 0.);
}

} // extern "C"
