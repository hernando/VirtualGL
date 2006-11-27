/* Copyright (C)2004 Landmark Graphics
 * Copyright (C)2005, 2006 Sun Microsystems, Inc.
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

#ifndef __FAKER_CONFIG_H
#define __FAKER_CONFIG_H

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "rr.h"
#include "rrutil.h"
#include "rrlog.h"
#include "rrsunray.h"
#include <stdio.h>
#include <X11/X.h>
#include <X11/keysym.h>

#define DEFLOQUAL 90
#define DEFHIQUAL 95
#define DEFLOSUBSAMP 4
#define DEFHISUBSAMP 1

#define MAXSTR 256

class Config
{
	public:

		Config() {_set=false;  _env=NULL;}
		~Config() {if(_env) {free(_env);  _env=NULL;}}
		bool isset(void) {return _set;}

	protected:

		bool newenv(char *env)
		{
			if(_env)
			{
				if(strlen(env)==strlen(_env) && !strncmp(env, _env, strlen(env)))
					return false;
				free(_env);  _env=NULL;
			}
			_env=strdup(env);
			return true;
		}

		bool _set;
		char *_env;
};

class ConfigBool : public Config
{
	public:

		ConfigBool() {_b=false;}
		ConfigBool& operator= (bool b) {set(b);  return *this;}
		bool operator= (ConfigBool &cb) {return cb._b;}
		operator bool() const {return _b;}

		bool get(const char *envvar)
		{
			char *temp=NULL;
			if((temp=getenv(envvar))!=NULL && strlen(temp)>0 && newenv(temp))
			{
				if(!strncmp(temp, "1", 1)) set(true);
				else if(!strncmp(temp, "0", 1)) set(false);
			}
			return _b;
		}

		void set(bool b)
		{
			_b=b;  _set=true;
		}

	private:

		bool _b;
};


class ConfigDouble : public Config
{
	public:

		ConfigDouble() {_d=0.;  _usebounds=false;  _min=_max=0.;}
		ConfigDouble& operator= (double d) {set(d);  return *this;}
		double operator= (ConfigDouble &cd) {return cd._d;}
		operator double() const {return _d;}

		void setbounds(double min, double max)
		{
			_min=min;  _max=max;  _usebounds=true;
		}

		double get(const char *envvar)
		{
			char *temp=NULL;
			if((temp=getenv(envvar))!=NULL && strlen(temp)>0 && newenv(temp))
			{
				char *t=NULL;  double dtemp=strtod(temp, &t);
				if(t && t!=temp) set(dtemp);
			}
			return _d;
		}

		void set(double d)
		{
			if((d>_min && d<_max) || !_usebounds) _d=d;
			_set=true;
		}

	private:

		double _d, _min, _max;
		bool _usebounds;
};

class ConfigInt : public Config
{
	public:

		ConfigInt() {_i=0;  _usebounds=false;  _min=_max=0;}
		ConfigInt& operator= (int i) {set(i);  return *this;}
		int operator= (ConfigInt &ci) {return ci._i;}
		operator int() const {return _i;}

		void setbounds(int min, int max)
		{
			_min=min;  _max=max;  _usebounds=true;
		}

		int get(const char *envvar)
		{
			char *temp=NULL;
			if((temp=getenv(envvar))!=NULL && strlen(temp)>0 && newenv(temp))
			{
				char *t=NULL;  int itemp=strtol(temp, &t, 10);
				if(t && t!=temp) set(itemp);
			}
			return _i;
		}

		void set(int i)
		{
			if((i>=_min && i<=_max) || !_usebounds) _i=i;
			_set=true;
		}

	protected:

		int _i, _min, _max;
		bool _usebounds;
};

class ConfigSubsamp : public ConfigInt
{
	public:

		ConfigSubsamp() {ConfigInt::setbounds(0, 16);}

		ConfigSubsamp& operator= (int i)
		{
			if(isPow2(i)) set(i);  return *this;
		}

		int get(const char *envvar)
		{
			char *temp=NULL;
			if((temp=getenv(envvar))!=NULL && strlen(temp)>0 && newenv(temp))
			{
				char *t=NULL;  int itemp=strtol(temp, &t, 10);
				if(t && t!=temp)
				{
					switch(itemp)
					{
						case 444: case 11: case 0: case 1:   set(1);  break;
						case 422: case 21: case 2:           set(2);  break;
						case 411: case 420: case 22: case 4: set(4);  break;
						case 410: case 42: case 8:           set(8);  break;
						case 44:  case 16:                   set(16);  break;
					}
				}
			}
			return _i;
		}
};

class ConfigNP : public ConfigInt
{
	public:

		ConfigNP()
		{
			ConfigInt::setbounds(0, 1024);
			_i=adjust(0);
		}

		ConfigNP& operator= (int i) {set(adjust(i));  return *this;}

		int get(const char *envvar)
		{
			char *temp=NULL;
			if((temp=getenv(envvar))!=NULL && strlen(temp)>0 && newenv(temp))
			{
				char *t=NULL;  int itemp=strtol(temp, &t, 10);
				if(t && t!=temp && itemp>=0) set(adjust(itemp));
			}
			return _i;
		}

	private:

		int adjust(int np)
		{
			np=min(np, min(numprocs(), MAXPROCS));
			if(np==0)
			{
				np=min(numprocs(), MAXPROCS);	 if(np>1) np--;
			}
			return np;
		}

};

class ConfigCompress : public ConfigInt
{
	public:

		ConfigCompress()
		{
			ConfigInt::setbounds(0, RR_COMPRESSOPT-1);  reload();
		}

		ConfigCompress(Display *dpy, int issunray=-1)
		{
			ConfigInt::setbounds(0, RR_COMPRESSOPT-1);  reload();
			if(issunray>=0) setdefault(dpy, issunray);
			else setdefault(dpy);
		}

		ConfigCompress& operator= (enum rrcomp i) {set((int)i);  return *this ;}

		int setdefault(Display *dpy)
		{
			int issunray=RRSunRayQueryDisplay(dpy);
			return setdefault(dpy, issunray);
		}

		int setdefault(Display *dpy, int issunray)
		{
			if(!isset())
			{
				if(issunray==RRSUNRAY_WITH_ROUTE) set(RRCOMP_SUNRAY);
				else
				{
					const char *dstr=DisplayString(dpy);
					if((strlen(dstr) && dstr[0]==':') || (strlen(dstr)>5
						&& !strnicmp(dstr, "unix", 4))) set(RRCOMP_NONE);
					else set(RRCOMP_JPEG);
				}
			}
			return issunray;
		}

		int get(const char *envvar)
		{
			char *temp=NULL;
			if((temp=getenv(envvar))!=NULL && strlen(temp)>0 && newenv(temp))
			{
				char *t=NULL;  int itemp=strtol(temp, &t, 10);
				if(t && t!=temp && itemp>=0 && itemp<RR_COMPRESSOPT) set(itemp);
				else if(!stricmp(temp, "raw")) set(RRCOMP_NONE);
				else if(!stricmp(temp, "none")) set(RRCOMP_NONE);
				else if(!stricmp(temp, "jpeg")) set(RRCOMP_JPEG);
				else if(!stricmp(temp, "sr")) set(RRCOMP_SUNRAY);
				else if(!stricmp(temp, "srlossless")) set(RRCOMP_SUNRAY_LOSSLESS);
			}
			return _i;
		}

		void reload(void) {get("VGL_COMPRESS");}
};

class ConfigString : public Config
{
	public:

		ConfigString() {_s=NULL;}
		ConfigString& operator= (char *s) {set(s);  return *this;}
		char* operator= (ConfigString &cs) {return cs._s;}
		~ConfigString() {if(_s) free(_s);}
		operator char*() const {return _s;}
		operator bool() const {return _s!=NULL;}

		char *get(const char *envvar)
		{
			char *temp=NULL;
			if((temp=getenv(envvar))!=NULL && strlen(temp)>0 && newenv(temp))
			{
				for(int i=0; i<(int)strlen(temp); i++)
					if(temp[i]!=' ' && temp[i]!='\t')
					{
						temp=&temp[i];  break;
					}
				if(strlen(temp)>0)
				{
					for(int i=0; i<(int)strlen(temp); i++)
						if(temp[i]==' ' || temp[i]=='\t')
							{temp[i]='\0';  break;}
					set(temp);
				}
			}
			return _s;
		}

		void set(char *s)
		{
			if(!s) return;
			if(_s) free(_s);  _s=strdup(s);  _set=true;
		}

	private:

		char *_s;
};

class FakerConfig
{
	public:

		FakerConfig(void) : currentqual(hiqual), currentsubsamp(hisubsamp)
		{
			// Defaults
			client=NULL;
			fps.setbounds(0.0, 1000000.0);
			gamma=true;
			gllib=NULL;
			glp=false;
			gui=true;
			guikey=XK_F9;
			guimod=ShiftMask|ControlMask;
			localdpystring=(char *)":0";
			loqual.setbounds(1, 100);
			loqual=DEFLOQUAL;
			losubsamp=DEFLOSUBSAMP;
			hiqual.setbounds(1, 100);
			hiqual=DEFHIQUAL;
			hisubsamp=DEFHISUBSAMP;
			currentqual=hiqual;
			currentsubsamp=hisubsamp;
			port.setbounds(0, 65535);
			readback=true;
			spoil=true;
			ssl=false;
			x11lib=NULL;
			tilesize.setbounds(8, 1024);
			tilesize=RR_DEFAULTTILESIZE;
			transpixel.setbounds(0, 255);
			vendor=NULL;
			reloadenv();
		}

		void reloadenv(void)
		{
			// Fetch values from environment
			gllib.get("VGL_GLLIB");
			x11lib.get("VGL_X11LIB");
			client.get("VGL_CLIENT");
			localdpystring.get("VGL_DISPLAY");
			#ifdef USEGLP
			if(localdpystring &&
				(localdpystring[0]=='/' || !strnicmp(localdpystring, "GLP", 3)))
				glp=true;
			#endif
			loqual.get("VGL_LOQUAL");
			losubsamp.get("VGL_LOSUBSAMP");
			hiqual.get("VGL_QUAL");
			hisubsamp.get("VGL_SUBSAMP");
			sethiqual();
			spoil.get("VGL_SPOIL");
			ssl.get("VGL_SSL");
			port.get("VGL_PORT");
			if(!port.isset()) port=ssl?RR_DEFAULTSSLPORT:RR_DEFAULTPORT;
			usewindow.get("VGL_WINDOW");
			if(glp) usewindow=false;
			sync.get("VGL_SYNC");
			np.get("VGL_NPROCS");
			autotest.get("VGL_AUTOTEST");
			gamma.get("VGL_GAMMA");
			transpixel.get("VGL_TRANSPIXEL");
			tilesize.get("VGL_TILESIZE");
			trace.get("VGL_TRACE");
			readback.get("VGL_READBACK");
			verbose.get("VGL_VERBOSE");
			guikeyseq.get("VGL_GUI");
			if(guikeyseq && strlen(guikeyseq)>0)
			{
				if(!stricmp(guikeyseq, "none")) gui=false;
				else
				{
					unsigned int mod=0, key=0;
					for(unsigned int i=0; i<strlen(guikeyseq); i++)
						guikeyseq[i]=tolower(guikeyseq[i]);
					if(strstr(guikeyseq, "ctrl")) mod|=ControlMask;
					if(strstr(guikeyseq, "alt")) mod|=Mod1Mask;
					if(strstr(guikeyseq, "shift")) mod|=ShiftMask;
					if(strstr(guikeyseq, "f10")) key=XK_F10;
					else if(strstr(guikeyseq, "f11")) key=XK_F11;
					else if(strstr(guikeyseq, "f12")) key=XK_F12;
					else if(strstr(guikeyseq, "f1")) key=XK_F1;
					else if(strstr(guikeyseq, "f2")) key=XK_F2;
					else if(strstr(guikeyseq, "f3")) key=XK_F3;
					else if(strstr(guikeyseq, "f4")) key=XK_F4;
					else if(strstr(guikeyseq, "f5")) key=XK_F5;
					else if(strstr(guikeyseq, "f6")) key=XK_F6;
					else if(strstr(guikeyseq, "f7")) key=XK_F7;
					else if(strstr(guikeyseq, "f8")) key=XK_F8;
					else if(strstr(guikeyseq, "f9")) key=XK_F9;
					if(key) guikey=key;  guimod=mod;
					gui=true;
				}
			}
			fps.get("VGL_FPS");
			vendor.get("VGL_XVENDOR");
		}

		void setloqual(void)
		{
			currentqual=loqual;  currentsubsamp=losubsamp;
		}

		void sethiqual(void)
		{
			currentqual=hiqual;  currentsubsamp=hisubsamp;
		}

		#define prconfint(i) rrout.println(#i" = %d", (int)i);
		#define prconfstr(s) rrout.println(#s" = %s", (char *)s);
		#define prconfdbl(d) rrout.println(#d" = %f", (double)d);

		ConfigBool autotest;
		ConfigString client;
		ConfigInt &currentqual;
		ConfigSubsamp &currentsubsamp;
		ConfigDouble fps;
		ConfigBool gamma;
		ConfigString gllib;
		bool glp;
		ConfigBool gui;
		unsigned int guikey;
		ConfigString guikeyseq;
		unsigned int guimod;
		ConfigInt hiqual;
		ConfigSubsamp hisubsamp;
		ConfigString localdpystring;
		ConfigInt loqual;
		ConfigSubsamp losubsamp;
		ConfigNP np;
		ConfigInt port;
		ConfigBool readback;
		ConfigBool spoil;
		ConfigBool ssl;
		ConfigBool sync;
		ConfigInt tilesize;
		ConfigBool trace;
		ConfigInt transpixel;
		ConfigBool usewindow;
		ConfigString vendor;
		ConfigBool verbose;
		ConfigString x11lib;
};

#endif
