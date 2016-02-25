/*
 * Check clock accuracy on Celestron NexStar/+ hand control
 * Copyright (c) 2016, Francis J. A. Pinteric
 * All Rights Reserved.
 * This software is licensed under the GNU General Public License Version 2.
 * Please see http://www.gnu.org//licenses/old-licenses/gpl-2.0.html for details.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
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

#define	VERSION			((00<<16)|(95<<8)|(1))
#define	VERSION_MAJOR	((VERSION>>16)&0xFF)
#define	VERSION_MINOR	((VERSION>>8)&0xFF)
#define	VERSION_REV		( VERSION    &0xFF)

/* Device commands */

#define	DEV_OPEN	0
#define	DEV_CLOSE	1

/* non-celestron commands */
#define	OPT_HELP		0x7000
#define	OPT_VERSION		0x7001
#define	OPT_COPYRIGHT	0x7002


char	*devname = "/dev/ttyUSB0";
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
	static char *c = "Copyright (C) 2016 Francis J. A. Pinteric\n"
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

int read_clock(char *buf)
{
	if(dev_write("h", 1) != 1 ) {
		errlog(2, "cmd_getloc failed to write");
		return 0;
	}
	if(dev_read(buf, 9) != 9 ) {
		errlog(2, "cmd_gettime failed to read");
		return 0;
	}
	return 1;
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

int measure_clock()
{
	struct timeval t1, t2;
	struct tm tm;
	unsigned long diff;
	char buf[9];
	
	if( gettimeofday(&t1, NULL) == 0 )
		return 0;
	if( read_clock(buf) == 0 )
		return 0;
	if( gettimeofday(&t2, NULL) == 0 )
		return 0;
	diff = ((t2.tv_sec%86400)*1000000 + t2.tv_usec) -
				((t1.tv_sec%86400)*1000000 + t1.tv_usec);
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
		}
	}
}
