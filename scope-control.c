/*
 * Scope control for Celestron NexStar hand control
 * Copyright (c) 2015, Francis J. A. Pinteric
 * All Rights Reserved.
 * This software is licensed under the GNU General Public License Version 2.
 * Please see http://www.gnu.org//licenses/old-licenses/gpl-2.0.html for details.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <termios.h>
#include <unistd.h>
#include <math.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include <errno.h>

/* */

#define	VERSION			((00<<16)|(95<<8)|(2))
#define	VERSION_MAJOR	((VERSION>>16)&0xFF)
#define	VERSION_MINOR	((VERSION>>8)&0xFF)
#define	VERSION_REV		( VERSION    &0xFF)

/* Device commands */

#define	DEV_OPEN	0
#define	DEV_CLOSE	1

/* commands */
#define	OPT_ECHO		0x8001
#define	OPT_DEVICE		0x8002
#define	OPT_GETLOC		0x8003
#define	OPT_SETLOC		0x8004
#define	OPT_GOTOINPROG	0x8005
#define	OPT_ALIGNCOMPL	0x8006
#define	OPT_GETTIME		0x8007
#define	OPT_SETTIME		0x8008
#define	OPT_GETRA		0x8009
#define	OPT_GETPRA		0x800A
#define	OPT_ALTAZ		0x800B
#define	OPT_PALTAZ		0x800C
#define	OPT_GOTORA		0x800D
#define	OPT_GOTOPRA		0x800E
#define	OPT_GOTOALTAZ	0x800F
#define	OPT_GOTOPALTAZ	0x8010
#define	OPT_GETTRACK	0x8011
#define	OPT_SETTRACK	0x8012
#define	OPT_SYNC		0x8013
#define	OPT_PSYNC		0x8014
#define	OPT_CANCELGOTO	0x8015
#define	OPT_GETVERSION	0x8016
#define	OPT_DEVVERSION	0x8017
#define OPT_GETMODEL	0x8018
#define	OPT_SLEW		0x8019
/* non-celestron commands */
#define	OPT_HELP		0x7000
#define	OPT_VERSION		0x7001
#define	OPT_COPYRIGHT	0x7002


char	*devname = NULL;
int		devfd;
int		devstatus = -1;
int		syserr = 0;
struct termios termios_new, termios_original;

/* standard file descriptors */
FILE	*infile;
FILE	*outfile;
FILE	*errfile;

/* commands and options */
struct option long_options[] = {
		{"version", no_argument, 0, OPT_VERSION},
		{"copyright", no_argument, 0, OPT_COPYRIGHT},
		{"help", no_argument, 0, OPT_HELP},
		{"echo",	required_argument,	0,	OPT_ECHO},
		{"device",	required_argument,	0,	OPT_DEVICE},
		{"getlocation", no_argument,	0,	OPT_GETLOC},
		{"setlocation", required_argument, 0, OPT_SETLOC},
		{"gettime",		no_argument,	0,	OPT_GETTIME},
		{"settime",		required_argument, 0,	OPT_SETTIME},
		{"getra",	no_argument,		0,	OPT_GETRA},
		{"precise-getra",	no_argument,	0,	OPT_GETPRA},
		{"getazalt",	no_argument,		0,	OPT_ALTAZ},
		{"precise-getazalt",	no_argument,	0,	OPT_PALTAZ},
		{"gotora",	required_argument,		0,	OPT_GOTORA},
		{"precise-gotora",	required_argument,	0,	OPT_GOTOPRA},
		{"gotoazalt",	required_argument,		0,	OPT_GOTOALTAZ},
		{"precise-gotoazalt",	required_argument,	0,	OPT_GOTOPALTAZ},
		{"gettracking",	no_argument,	0,	OPT_GETTRACK},
		{"settracking", required_argument,	0,	OPT_SETTRACK},
		{"isgotoinprogress", no_argument, 0, OPT_GOTOINPROG},
		{"isalignmentcomplete", no_argument, 0, OPT_ALIGNCOMPL},
		{"sync", required_argument, 0, OPT_SYNC},
		{"precise-sync", required_argument, 0, OPT_PSYNC},
		{"cancelgoto", no_argument, 0, OPT_CANCELGOTO},
		{"getversions", no_argument, 0, OPT_GETVERSION},
		{"deviceversion", required_argument, 0, OPT_DEVVERSION},
		{"getmodel", no_argument, 0, OPT_GETMODEL},
		{"slew", required_argument, 0, OPT_SLEW},
		{0,			0,					0,	0}
};

/*
 * Ripped from standard fja library help
 */

void usage(FILE *f, char *argv0, struct option *lp)
{
	struct option *pp;
	
	fprintf(f, "Usage: %s\n", argv0);
	for(pp = lp; pp->name != NULL; pp++) {
		fprintf(f, "\t\t[--%s", pp->name);
		if( pp->has_arg == required_argument )
			fprintf(f, " <parameter>");
		if( pp->has_arg == optional_argument )
			fprintf(f, "[parameter]");
		fprintf(f, "]\n");
	}
	fprintf(f, "Notes:\n\t1. <parameter> indicates a required argument\n"
				"\t2. [parameter] indicates an optional argument\n"
				);
}

void version(FILE *f, char *argv0)
{
	fprintf(f, "%s version %d.%d.%d\n",
		argv0, VERSION_MAJOR, VERSION_MINOR, VERSION_REV);
}

void copyright(FILE *f)
{
	static char *c = "Copyright (C) 2015 Francis J. A. Pinteric\n"
"License GPLv2: GNU GPL version 2 <http://gnu.org/licenses/gpl-2.0.html>.\n"
"This is free software: you are free to change and redistribute it.\n"
"There is NO WARRANTY, to the extent permitted by law\n";
	fprintf(f, c);
}

void errlog(int type, const char *format, ...)
{
	va_list ap;
	
	va_start(ap, format);
	fprintf(errfile, "Fail type=%d ", type);
	vfprintf(errfile, format, ap);
	fprintf(errfile, "\n");
	va_end(ap);
	syserr = 1;
}

int dev_control(int cmd, char *serial_device)
{

	if( cmd == DEV_OPEN ) {
		if( devstatus != -1 )
			return -1;
		if( (devfd = open(serial_device, O_RDWR|O_NOCTTY)) < 0 ) {
			devstatus = -1;
			errlog(0, "serial port open %s failed\n", serial_device);
			return -1;
		}
		if (0 && (fcntl(devfd, F_SETFL, O_NONBLOCK) < 0)) {
			errlog(0, "serial port %s fcntl(O_NONBLOCK) failed: s\n", serial_device, strerror(errno));
		} else { 
			if (tcgetattr(devfd,&termios_original) < 0) {
			errlog(0, "serial port %s tcgetattr(devfd,&termios_original) failed: %s\n", serial_device, strerror(errno));
			} else {
				memset(&termios_new, 0, sizeof(termios_new));
				termios_new.c_cflag = CS8    |  // 8 data bits
									// no parity because PARENB is not set
									CLOCAL |  // Ignore modem control lines
									CREAD;    // Enable receiver
				cfsetospeed(&termios_new, B9600);
				termios_new.c_lflag = 0;
				termios_new.c_cc[VTIME] = 0;
				termios_new.c_cc[VMIN] = 1;
				if (tcsetattr(devfd,TCSAFLUSH, &termios_new) == 0) {
					devstatus = 0;
					devname = serial_device;
					return 0;
				}
			}
			close(devfd);
			devfd = -1;
			devstatus = -1;
		}
	}
	if( cmd == DEV_CLOSE ) {
		if ( devstatus == -1 )
			return -1;
		if( tcsetattr(devfd, TCSAFLUSH, &termios_original) < 0) {
			/* don't care */
		}
		close(devfd);
		devstatus = -1;
		devfd = 0;
		devname = "";
		return 0;
	}
	return -1;
}

int dev_write(const void *bufp, size_t len)
{
	return write(devfd, bufp, len);
}

int dev_read(void *bufp, size_t rlen)
{
	int l, len = 0, tlen=rlen;

	while((l = read(devfd, &((char*)bufp)[len], rlen)) != rlen) {
		rlen -= l;
		len += l;
	}
	return tlen;
}

void cmd_echo(char *arg)
{
	static char buf[2];
	int i;

	buf[0] = 'K';
	buf[1] = arg[0];
	if( dev_write(buf, 2) != 2) {
		errlog(1, "cmd_echo failed to write");
		return;
	}
	if( dev_read(buf, 2) != 2) {
		errlog(1, "cmd_echo failed to read");
		return;
	}
	fprintf(outfile, "cmdecho read %c%c\n", buf[0], buf[1]);
}

void cmd_getloc()
{
	char	buf[9];

	if(dev_write("w", 1) != 1 ) {
		errlog(2, "cmd_getloc failed to write");
		return;
	}
	if(dev_read(buf, 9) != 9 ) {
		errlog(2, "cmd_getloc failed to read");
		return;
	}
	fprintf(outfile, "Location %s %02dd %02dm %02ds %c %03dd %02dm %02ds %c\n",
		buf[8] == '#' ? "valid" : "invalid",
		buf[0], buf[1], buf[2], buf[3] == 0 ? 'N' : 'S',
		buf[4], buf[5], buf[6], buf[7] == 0 ? 'E' : 'W');
}

void cmd_setloc(char *str)
{
	int lon_d, lon_m, lon_s, lon_ew, lat_d, lat_m, lat_s, lat_ns;
	int c;
	char buf[9];

	c = sscanf(str, "%d %d %d %d %d %d",
		&lat_d, &lat_m, &lat_s, &lon_d, &lon_m, &lon_s);
	if ( c != 6 ) {
		errlog(3, "cmd_setloc invalid latitude/longitude entry");
		return;
	}
	if( lat_d < 0 ) {
		lat_ns = 1; /* south */
		lat_d = -lat_d;
	} else
		lat_ns = 0; /* north */
	if( lon_d < 0 ) {
		lon_ew = 1; /* west */
		lon_d = -lon_d;
	} else
		lon_ew = 0; /* east */
	buf[0] = 'W';
	buf[1] = lat_d;
	buf[2] = lat_m;
	buf[3] = lat_s;
	buf[4] = lat_ns;
	buf[5] = lon_d;
	buf[6] = lon_m;
	buf[7] = lon_s;
	buf[8] = lon_ew;
	if ( dev_write(buf, 9) != 9 ) {
		errlog(3, "cmd_setloc return error on write");
		return;
	}
	if( dev_read(buf, 1) != 1 ) {
		errlog(3, "cmd_setloc returned error on read");
		return;
	}
	fprintf(outfile, "cmd_setloc set location %s\n", buf[0] == '#' ? "successfully" : "error");
}

void cmd_gettime()
{
	char	buf[9];

	if(dev_write("h", 1) != 1 ) {
		errlog(2, "cmd_getloc failed to write");
		return;
	}
	if(dev_read(buf, 9) != 9 ) {
		errlog(2, "cmd_gettime failed to read");
		return;
	}
	fprintf(outfile, "Time %s %02dh %02dm %02ds %02d-%02d-%02d %02d %s time\n",
		buf[8] == '#' ? "valid" : "invalid",
		buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6], buf[7] == 0 ? "Standard" : "Summer");
}

void cmd_settime(char *str)
{
	int hour, min, sec, mon, day, year, gmtoffs, dst;
	int c;
	struct tm *tm;
	time_t t;
	extern time_t timezone;
	char buf[9];

	if( strcmp("localtime", str) == 0 ) {
		t = time(NULL);
		tm = localtime((time_t*)&t);
		sec = tm->tm_sec;
		min = tm->tm_min;
		hour = tm->tm_hour;
		day = tm->tm_mday;
		mon = tm->tm_mon + 1;
		year = tm->tm_year % 100;
		dst = tm->tm_isdst;
		gmtoffs = -(timezone / 3600);
	} else {
		c = sscanf(str, "%d %d %d %d %d %d %d %d",
			&hour, &min, &sec, &mon, &day, &year, &gmtoffs, &dst);
		if ( c != 8 ) {
			errlog(4, "cmd_settime invalid time-date format");
			return;
		}
	}
	buf[0] = 'H';
	buf[1] = hour;
	buf[2] = min;
	buf[3] = sec;
	buf[4] = mon;
	buf[5] = day;
	buf[6] = year;
	buf[7] = gmtoffs;
	buf[8] = dst;
	if ( dev_write(buf, 9) != 9 ) {
		errlog(4, "cmd_settime return error on write");
		return;
	}
	if( dev_read(buf, 1) != 1 ) {
		errlog(4, "cmd_settime returned error on read");
		return;
	}
	fprintf(outfile, "cmd_settime set time/date %s\n", buf[0] == '#' ? "successfully" : "error");
}

/*
 * Get and Set mount tracking mode
 * Mode:
 * 	0 - tracking off
 *  1 - Alt-Az
 *  2 - EQ North
 *  3 - EQ South
 */

char	*track_modes[] = { "Off", "Alt-Azimuth", "EQNorth", "EQSouth"};

void cmd_gettrack()
{
	char	buf[2], *m;

	if(dev_write("t", 1) != 1 ) {
		errlog(2, "cmd_gettrack failed to write");
		return;
	}
	if( (dev_read(buf, 2) != 2) || (buf[1] != '#') ) {
		errlog(2, "cmd_gettrack failed to read");
		return;
	}
	if( buf[0] > 3 || buf[0] < 0 )
		m = "Unknown";
	else
		m = track_modes[buf[0]];
	fprintf(outfile, "Tracking mode: %s\n", m);
}

void cmd_settrack(char *type)
{
	int i;
	char buf, cmd[2] = { 'T', 0  };
	
	for(i = 0; i < 4; i++) {
		if( strcmp(track_modes[i], type) == 0 )
			break;
	}
	if( i > 3 ) {
		errlog(0, "Set track passed unknown mode: %s\n", type);
		return;
	}
	cmd[1] = i;
	if(dev_write(cmd, 2) != 2 ) {
		errlog(2, "cmd_settrack failed to write");
		return;
	}
	if( (dev_read(&buf, 1) != 1) || (buf != '#') ) {
		errlog(2, "cmd_gettrack failed to read");
		return;
	}
	fprintf(outfile, "Tracking mode set to %s\n", track_modes[i]);
}

void cmd_isgotoinprogress()
{
	char buf[2];

	if(dev_write("L", 1) != 1 ) {
		errlog(0, "cmd_isgotoinprogress failed to write");
		return;
	}
	if( (dev_read(&buf, 2) != 2) || (buf[1] != '#') ) {
		errlog(2, "cmd_isgotinprogress failed to read");
		return;
	}
	fprintf(outfile, "Is Goto In Progress? %s.\n", buf[0] == '1' ? "Yes" : "No");
}

void cmd_isaligncomplete()
{
	char buf[2];
	
	if(dev_write("J", 1) != 1 ) {
		errlog(0, "cmd_isaligncomplete failed to writer");
		return;
	}
	if( (dev_read(&buf, 2) != 2) || (buf[1] != '#') ) {
		errlog(2, "cmd_isaligncomplete failed to read");
		return;
	}
	fprintf(outfile, "Is Alignment Complete? %s.\n", buf[0] == 1 ? "Yes" : "No");
}

/*
 * parse [+-]#+[dh]#+m#+[.#+]s into an angle and return
 * individual components
 * if next is not NULL *next returns pointer to next character in buffer after
 * completion or last character seen on error.
 * On error, returns NaN and contents of *dh, *min and *sec are undefined
 */
double convert2angle(char *buf, char **next, int *dh, int *min, double *sec, int *rerr)
{
	char *bp = buf, c;
	int sign = 1, state = 0, type = 0, done = 0;
	int number1=0, number2=0, number3=0;
	double fraction=0.0,part=0.0, rvalue;

	while((c = *bp++) != 0) {
		switch(state) {
		case 0: /* looking for #,  + or - ; ignore space */
			if( isspace(c) )
				continue;
			if( c == '+' ) {
				sign = 1;
				state = 1;
				continue;
			}
			if( c == '-' ) {
				sign = -1;
				state = 1;
				continue;
			}
			state = 1;
			/* fall through to numbers only */
		case 1:
			if( !(c >= '0' && c <= '9') )
				goto err;
			for(number1 = 0; c >= '0' && c <= '9'; c = *bp++)
				number1 = number1*10 + (c - '0');
			state = 2;
			--bp; /* point back to char that broke the loop */
			break;
		case 2: /* must be one of 'd', 'D', 'H' or 'h' */
			if( c == 'd' || c == 'D' )
				type = 1;
			else if (c == 'h' || c == 'H' )
				type = 2;
			else
				goto err;
			state = 3;
			break;
		case 3:
			if( isspace(c))
				continue;
			state = 4;
			/* fall through */
		case 4:
			if( !(c >= '0' && c <= '9') )
				goto err;
			for(number2 = 0; c >= '0' && c <= '9'; c = *bp++)
				number2 = number2*10 + (c - '0');
			state = 5;
			--bp; /* point back to char that broke the loop */
			break;
		case 5: /* must be m or M */
			if( c != 'm' && c != 'M' )
				goto err;
			state = 6;
			break;
		case 6:
			if( isspace(c))
				break;
			state = 7;
			/* fall through */
		case 7:
			if( !(c >= '0' && c <= '9') )
				goto err;
			for(number3 = 0; c >= '0' && c <= '9'; c = *bp++)
				number3 = number3*10 + (c - '0');
			state = 8;
			--bp; /* point back to char that broke the loop */
			break;
		case 8: /* must be . s or S */
			fraction = 0.0;
			if( c == 's' || c == 'S' ) {
				done = 1;
				break;
			}
			if( c != '.' )
				goto err;
			state = 9;
			break;
		case 9: /* grab fraction */
			part = 0.1;
			fraction = 0.0;
			while( c >= '0' && c <= '9' ) {
				fraction += ((double)(c - '0'))*part;
				part /= 10.0;
				c = *bp++;
			}
			if( c != 's' && c != 'S' )
				goto err;
			done = 1;
			break;
		}
		if( done )
			break;
	}
	if( !done )
		goto err;
	/* success! return values */
	if( next )
		*next = bp;
	if( dh != NULL )
		*dh = number1*sign;
	if( min != NULL )
		*min = number2*sign;
	if( sec != NULL )
		*sec = (double)sign*((double)number3 + fraction);
	rvalue = (double)number1 + ((double)number2 + ((double)number3 + fraction)/60.0)/60.0;
	if( type == 1 )
		rvalue /= 360.0;
	else /* type == 2 */
		rvalue /= 24.0;
	rvalue *= (double)sign;
	*rerr = 0;
	return rvalue;
err:
	if( next )
		*next = bp;
	/* process error here */
	*rerr = state;
	return 0.0;
}

/*
 * convert hhmmss or ddmmss to degrees. 
 * type==0 for ddmms type=1 for hhmmss
 */
int convert2position(char *buf, int type, double *rvalue1, double *rvalue2)
{
	char *fmt;
	int	dh1, min1, dh2, min2, err;
	double second1, second2;

	*rvalue1 = convert2angle(buf, &fmt, &dh1, &min1, &second1, &err);
	*rvalue2 = convert2angle(fmt, NULL, &dh2, &min2, &second2, &err);
 	return 0;
}

void convert2hhmmss(char *buf, double value, int hour)
{
	int dh, m, s, frac, minus = 0;
	char *ticks[2] = { "dms", "hms" };

	if( value < 0.0 ) {
		minus = 1;
		value = -value;
	}
	dh = floor(value); value = (value - dh)*60;
	m  = floor(value); value = (value -  m)*60;
	s  = floor(value); value = (value -  s)*1000;
	frac = floor(value);
	sprintf(buf, "%c%03d%c %02d%c %02d.%03d%c", (minus == 0) ? '+' : '-',
		dh, ticks[hour][0], m, ticks[hour][1], s, frac, ticks[hour][2]);
}

/* decode 16 or 32 bit positional in RA or ALTAZIMUTH */
char *decode(char *buf, char cmd, double *ab)
{
	long long v[2];
	static char rbuf[64];
	char buf1[17], buf2[17];
	double m[2], r;

	switch(cmd) {
	default:
		return "unimplemented";
	case 'E': case 'Z': /* 16 bit precision */
		v[0] = strtoll(&buf[0], NULL, 16);
		v[1] = strtoll(&buf[5], NULL, 16);
		ab[0] = ((double)v[0]/65536.0);
		ab[1] = ((double)v[1]/65536.0);
		m[0] = (cmd == 'E')  ? 24 : 360;
		m[1] = 360;
		break;
	case 'e': case 'z': /* 24 bit precision */
		v[0] = strtoll(&buf[0], NULL, 16);
		v[1] = strtoll(&buf[9], NULL, 16);
		ab[0] = ((double)v[0]/4294967296.0);
		ab[1] = ((double)v[1]/4294967296.0);
		m[0] = (cmd == 'e')  ? 24 : 360;
		m[1] = 360;
		break;
	}
	if( (r = ab[1]*m[1]) > 180.0 )
		r =  r - 360;
	convert2hhmmss(buf1, ab[0]*m[0], (cmd == 'e' || cmd == 'E') ? 1 : 0);
	convert2hhmmss(buf2, ab[1] = r,  0);
	sprintf(rbuf, "%s %s", buf1, buf2);
	return rbuf;
}

void cmd_getposition(char *name, char cmd, int rlen)
{
	char buf[20];
	double ab[2];
	int l, len = 0;

	if(dev_write(&cmd, 1) != 1) {
		errlog(5, "%s cannot write command\n", name);
		return;
	}
	memset(buf, 0, sizeof(buf));
	if(dev_read(&buf, rlen) != rlen ) {
		errlog(5, "%s cannot read result\n", name);
		return;
	}
	fprintf(outfile, "%s returns %s %s\n", name, buf, decode(buf, cmd, ab));
	
}

/*
 * goto position
 * 	In azalt mode rvalue1 is azimith, rvalue2 is altitude
 * 	In ra mode rvalue1 is right ascension, rvalue2 is declination
 */
void cmd_gotoposition(char *name, char cmd, char *optarg)
{
	double rvalue1, rvalue2;
	long r1, r2;
	char buf[32], *fmt;

	if( cmd == 'R' || cmd == 'r' ) {
		if(convert2position(optarg, 1, &rvalue1, &rvalue2) < 0 )
			return;
	} else {
		if(convert2position(optarg, 0, &rvalue1, &rvalue2 )< 0 )
			return;
	}
	if (cmd == 'r' || cmd == 'b' ) { /* precise position */
		r1 = rvalue1*4294967296.0;
		r2 = rvalue2*4294967296.0;
		sprintf(buf, "%c%08X,%08X", cmd, ((int)r1)&0xFFFFFFFF,
										((int)r2)&0xFFFFFFFF);
	} else {
		sprintf(buf, "%c%04X,%04X", cmd, (int)(rvalue1*65536.0)&0xFFFF,
								(int)(rvalue2*65526.0)&0xFFFF);
	}
	fprintf(outfile, "%s converts `%s' to `'%s' ", name, optarg, buf);
	dev_write(buf, strlen(buf));
	dev_read(buf, 1);
	if( buf[0] == '#' )
		fprintf(outfile, "success\n");
	else
		fprintf(outfile, "fail\n");
}

void cmd_sync(char *name, char cmd, char *optarg)
{
	double rvalue1, rvalue2;
	long r1, r2;
	char buf[32], *fmt;

	if( convert2position(optarg, 1, &rvalue1, &rvalue2) < 0 )
		return;
	if (cmd == 's' ) { /* precise position */
		r1 = rvalue1*4294967296.0;
		r2 = rvalue2*4294967296.0;
		sprintf(buf, "%c%08X,%08X", cmd, ((int)r1)&0xFFFFFFFF,
										((int)r2)&0xFFFFFFFF);
	} else {
		sprintf(buf, "%c%04X,%04X", cmd, (int)(rvalue1*65536.0)&0xFFFF,
								(int)(rvalue2*65526.0)&0xFFFF);
	}
	fprintf(outfile, "%s converts `%s' to `'%s' ", name, optarg, buf);
	dev_write(buf, strlen(buf));
	dev_read(buf, 1);
	if( buf[0] == '#' )
		fprintf(outfile, "success\n");
	else
		fprintf(outfile, "fail\n");
	
}

void cmd_cancelgoto()
{
	char buf;
	
	fprintf(outfile, "cmd_cancelgoto ... ");
	dev_write("M", 1);
	dev_read(&buf, 1);
	if( buf == '#' )
		fprintf(outfile, "success\n");
	else
		fprintf(outfile, "fail\n");
}

void cmd_getversion()
{
	char buf[3];

	fprintf(outfile, "Hand Control Version is ");
	dev_write("V", 1);
	dev_read(&buf, 3);
	if( buf[2] == '#' )
		fprintf(outfile, "%d.%d\n", buf[0], buf[1]);
	else
		fprintf(outfile, "fail.\n");
}

void cmd_getdeviceversion(char *optarg)
{
	char *devs[] = {"AZM/RA Motor", "ALT/DEC Motor", "GPS", "RTC", NULL};
	char buf[8];
	int	i;

	for(i = 0; devs[i] != NULL; i++) {
		if( strcmp(optarg, devs[i]) == 0 )
			break;
	}
	fprintf(outfile, "Version of '%s' is ", devs[i]);
	if( devs[i] == NULL ) {
		fprintf(outfile, "unknown device\n");
		return;
	}
	buf[0] = 'P';
	buf[1] = 1;
	buf[2] = i + 16;
	buf[3] = 254;
	buf[4] = 0;
	buf[5] = 0;
	buf[6] = 0;
	buf[7] = 2;
	dev_write(buf, 8);
	dev_read(buf, 3);
	if( buf[2] == '#' )
		fprintf(outfile, "%d.%d\n", buf[0], buf[1]);
	else
		fprintf(outfile, "not connected\n");
}

void cmd_getmodel()
{
	char buf[2];
	char *models[] = {	"None (0)", "GPS Series", "None (2)", "i-Series",
						"i-Series SE", "CGE", "Advanced GT", "SLT",
						"None (8)", "CPC", "GT", "NexStar 4/5 SE", 
						"NexStar 6/8 SE"};
	char *model;
	int num_models = (sizeof(models)/sizeof(char*));
	
	dev_write("m", 1);
	if( (dev_read(&buf, 2) != 2) || (buf[1] != '#' ) ) {
		errlog(0, "cmd_getmodel failed on read.\n");
		return;
	}
	if (buf[0] <= 0 || buf[0] > num_models )
		model = "Unknown Model";
	else
		model = models[buf[0]];
	fprintf(outfile, "Telescope Model Celestron %s\n", model);
}

/*
 * Slew command
 * 	fv		fixed=0, variable=1
 * 	azalt	azimuth=0, altitude=1 (RA=0, declination=1)
 * 	rate	speed of slew. Sign of rate determines direction +/-
 * 			For fixed rate slew, value must be [-9, 9].
 * 			Value of zero means stop.
 */
 
void cmd_slew(int fv, int azalt, int rate)
{
	char	buf[8];
	
	if( (fv != 0 && fv != 1) || (azalt != 0 && azalt != 1) ) {
		fprintf(errfile, "Bad value for fixed/azalt. Aborting.\n");
		return;
	}
	buf[0] = 'P';
	buf[1] = 2|fv;
	buf[2] = 16|azalt;
	buf[3] = (6|(rate >= 0 ? 0 : 1)) + 30*(fv == 0);
	if( fv == 0 ) {
		buf[4] = abs(rate);
		buf[5] = 0;
	} else {
		buf[4] = abs(rate*4) >> 8;
		buf[5] = abs(rate*4) & 0xFF;
	}
	buf[6] = 0;
	buf[7] = 0;
	dev_write(buf, sizeof(buf));
	if( (dev_read(buf, 1) != 1) || (buf[0] != '#') ) {
		errlog(0, "cmd_slew failed on read\n");
		return;
	}
	fprintf(outfile, "Slew %s %s %d ok\n", fv == 0 ? "fixed" : "variable",
		azalt == 0 ? "azimuth/RA" : "altitude/declination", rate);
} 

void do_slew(char *optarg)
{
	int fv, d, rate;
	char buf1[32], buf2[32], *cp;

	for(cp = buf1; *optarg != ',' && *optarg != '\0'; *cp++ = *optarg++);
	if( *optarg == '\0' ) {
			errlog(0, "do_slew bad command syntax\n");
			return;
	}
	*cp = 0;
	++optarg;
	for(cp = buf2; *optarg != ',' && *optarg != '\0'; *cp++ = *optarg++);
	if( *optarg == '\0' ) {
			errlog(0, "do_slew bad command syntax\n");
			return;
	}
	*cp = 0;
	optarg++;
	rate = atoi(optarg);
	if( strcmp(buf1, "fixed") == 0 ) fv = 0; else
	if( strcmp(buf1, "variable") == 0 ) fv = 1;
	else {
		errlog(0, "do_slew arg1 must be `fixed' or `variable'\n");
		return;
	}
	if( strcmp(buf2, "azimuth") == 0 || strcmp(buf2, "RA") == 0 ) d = 0; else
	if( strcmp(buf2, "altitude") == 0 || strcmp(buf2, "declination") == 0 ) d = 1;
	else {
		errlog(0, "do_slew arg2 must be `azimuth', `RA', `altitude' or `declination'\n");
		return;
	}
	if( (fv == 0) && (rate < -9 || rate > 9) ) {
		errlog(0, "do_slew arg2 out of bounds\n");
		return;
	}
	cmd_slew(fv,  d, rate);
}

int main(int argc, char **argv)
{
	int	c;
	char *cmd_arg, *dir;

	infile = stdin;
	outfile = stdout;
	errfile = stderr;
	while(1) {
			int to_optind = optind ? optind : 1;
			int index = 0;
			c = getopt_long(argc, argv, "", long_options, &index);
			if( c == -1 )
				break;
			if( c == 0x3f ) /* invalid command detected */
				continue;
			switch(c) {
			case OPT_HELP:
				usage(errfile, basename(argv[0]), long_options);
				exit(0);
			case OPT_VERSION:
				version(outfile, basename(argv[0]));
				break;
			case OPT_COPYRIGHT:
				copyright(outfile);
				break;
			case OPT_ECHO:
				cmd_arg = optarg;
				cmd_echo(cmd_arg);
				break;
			case OPT_DEVICE: /* set and open device */
				devname = optarg;
				fprintf(outfile, "Communicating over port %s\n", devname);
				dev_control(DEV_OPEN, devname);
				break;
			case OPT_GETLOC:
				cmd_getloc();
				break;
			case OPT_SETLOC:
				cmd_setloc(optarg);
				break;
			case OPT_GETTIME:
				cmd_gettime();
				break;
			case OPT_SETTIME:
				cmd_settime(optarg);
				break;
			case OPT_GETRA:
				cmd_getposition("getra", 'E', 10);
				break;
			case OPT_GETPRA:
				cmd_getposition("precise-getra", 'e', 18);
				break;
			case OPT_ALTAZ:
				cmd_getposition("getaltaz", 'Z', 10);
				break;
			case OPT_PALTAZ:
				cmd_getposition("precise-getaltaz", 'z', 18);
				break;
			case OPT_GOTORA:
				cmd_gotoposition("gotora", 'R', optarg);
				break;
			case OPT_GOTOPRA:
				cmd_gotoposition("precise-gotora", 'r', optarg);
				break;
			case OPT_GOTOALTAZ:
				cmd_gotoposition("gotoaltaz", 'B', optarg);
				break;
			case OPT_GOTOPALTAZ:
				cmd_gotoposition("precise-gotoaltaz", 'b', optarg);
				break;
			case OPT_GETTRACK:
				cmd_gettrack();
				break;
			case OPT_SETTRACK:
				cmd_settrack(optarg);
				break;
			case OPT_GOTOINPROG:
				cmd_isgotoinprogress();
				break;
			case OPT_ALIGNCOMPL:
				cmd_isaligncomplete();
				break;
			case OPT_SYNC:
				cmd_sync("sync", 'S', optarg);
				break;
			case OPT_PSYNC:
				cmd_sync("precise-sync", 's', optarg);
				break;
			case OPT_CANCELGOTO:
				cmd_cancelgoto();
				break;
			case OPT_GETVERSION:
				cmd_getversion();
				break;
			case OPT_DEVVERSION:
				cmd_getdeviceversion(optarg);
				break;
			case OPT_GETMODEL:
				cmd_getmodel();
				break;
			case OPT_SLEW:
				do_slew(optarg);
				break;
			default:
				fprintf(errfile, "Unkown code 0x%x\n", c);
				break;
			}
			if( syserr != 0 ) {
				dev_control(DEV_CLOSE, NULL);
				exit(-1);
			}
	}
	return dev_control(DEV_CLOSE, NULL);
}
