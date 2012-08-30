/*
 * fbvncserver.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Started with original fbvncserver for the iPAQ and Zaurus.
 * 	http://fbvncserver.sourceforge.net/
 *
 * Modified by Jim Huang <jserv.tw@gmail.com>
 * 	- Simplified and sizing down
 * 	- Performance tweaks
 *
 * Modified by Steve Guo (letsgoustc)
 *  - Added keyboard/pointer input
 * 
 * Modified by Danke Xie (danke.xie@gmail.com)
 *  - Added input device search to support different devices
 *  - Added kernel vnckbd driver to allow full-keyboard input on 12-key hw
 *  - Supports Android framebuffer double buffering
 *  - Performance boost and fix GCC warnings in libvncserver-0.9.7
 *
 * NOTE: depends libvncserver.
 */

/* define the following to enable debug messages */
/* #define DEBUG */
/* #define DEBUG_VERBOSE */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <sys/stat.h>
#include <sys/sysmacros.h>             /* For makedev() */

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>

/* libvncserver */
#include "rfb/rfb.h"
#include "rfb/keysym.h"
#include "rfb/rfbregion.h"

#define APPNAME "fbvncserver"
#define VNC_DESKTOP_NAME "Android"

/* framebuffer */
#ifdef ANDROID
# define FB_DEVICE "/dev/graphics/fb0"
#else
# define FB_DEVICE "/dev/fb0"
#endif

static const char DEV_FMT[] = "/dev/input/event%d";

/* keywords to search for input devices by name */
static const char *KBD_PATTERNS[] = {
		"VNC",    /* VNC keyboard driver, 1st choice */
		"key",	  /* keypad */
		"qwerty", /* emulator */
		NULL};

static const char *TOUCH_PATTERNS[] = {
		"touch",  /* touchpad */
		"qwerty", /* emulator */
 		NULL};

/* default input device paths */
static char KBD_DEVICE[PATH_MAX] = "/dev/input/event2";
static char TOUCH_DEVICE[PATH_MAX] = "/dev/input/event1";

/* for compatibility of non-android systems */
#ifndef ANDROID
# define KEY_SOFT1 KEY_UNKNOWN
# define KEY_SOFT2 KEY_UNKNOWN
# define KEY_CENTER	KEY_UNKNOWN
# define KEY_SHARP KEY_UNKNOWN
# define KEY_STAR KEY_UNKNOWN
#endif

static struct fb_var_screeninfo scrinfo;
static int buffers = 2; /* mmap 2 buffers for android */
static int fbfd = -1;
static int kbdfd = -1;
static int touchfd = -1;
static unsigned short int *fbmmap = MAP_FAILED;
static unsigned short int *vncbuf;
static unsigned short int *fbbuf;
static int pixels_per_int;

__sighandler_t old_sigint_handler = NULL;

#define VNC_PORT 5901
static rfbScreenInfoPtr vncscr;

static int xmin, xmax;
static int ymin, ymax;

/* part of the frame differerencing algorithm. */
static struct varblock_t {
	int min_x;
	int min_y;
	int max_x;
	int max_y;
	int r_offset;
	int g_offset;
	int b_offset;
	int pixels_per_int;
} varblock;

/* event handler callback */
static void keyevent(rfbBool down, rfbKeySym key, rfbClientPtr cl);
static void ptrevent(int buttonMask, int x, int y, rfbClientPtr cl);

#ifdef DEBUG
# define pr_debug(fmt, ...) \
	 fprintf(stderr, fmt, ## __VA_ARGS__)
#else
# define pr_debug(fmt, ...) do { } while(0)
#endif

#ifdef DEBUG_VERBOSE
# define pr_vdebug(fmt, ...) \
	 pr_debug(fmt, ## __VA_ARGS__)
#else
# define pr_vdebug(fmt, ...) do { } while(0)
#endif

#define pr_info(fmt, ...) \
	fprintf(stdout, fmt, ## __VA_ARGS__)

#define pr_err(fmt, ...) \
	fprintf(stderr, fmt, ## __VA_ARGS__)

static void init_fb(void)
{
	size_t pixels;
	size_t bytespp;

	if ((fbfd = open(FB_DEVICE, O_RDONLY)) == -1) {
		perror("open");
		exit(EXIT_FAILURE);
	}

	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) != 0) {
		perror("ioctl");
		exit(EXIT_FAILURE);
	}

	pixels = scrinfo.xres * scrinfo.yres;
	bytespp = scrinfo.bits_per_pixel / 8;

	pr_info("xres=%d, yres=%d, "
			"xresv=%d, yresv=%d, "
			"xoffs=%d, yoffs=%d, "
			"bpp=%d\n", 
	  (int)scrinfo.xres, (int)scrinfo.yres,
	  (int)scrinfo.xres_virtual, (int)scrinfo.yres_virtual,
	  (int)scrinfo.xoffset, (int)scrinfo.yoffset,
	  (int)scrinfo.bits_per_pixel);

	fbmmap = mmap(NULL, buffers * pixels * bytespp,
			PROT_READ, MAP_SHARED, fbfd, 0);

	if (fbmmap == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}
}

static void cleanup_fb(void)
{
	if(fbfd != -1)
	{
		close(fbfd);
	}
}

static void init_kbd()
{
	if((kbdfd = open(KBD_DEVICE, O_RDWR)) == -1)
	{
		pr_err("cannot open kbd device %s\n", KBD_DEVICE);
		exit(EXIT_FAILURE);
	}
}

static void cleanup_kbd()
{
	if(kbdfd != -1)
	{
		close(kbdfd);
	}
}

static void init_touch()
{
    struct input_absinfo info;
        if((touchfd = open(TOUCH_DEVICE, O_RDWR)) == -1)
        {
                pr_err("cannot open touch device %s\n", TOUCH_DEVICE);
                exit(EXIT_FAILURE);
        }
    // Get the Range of X and Y
    if(ioctl(touchfd, EVIOCGABS(ABS_X), &info)) {
        pr_err("cannot get ABS_X info, %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    xmin = info.minimum;
    xmax = info.maximum;
    if (xmax)
    	pr_vdebug("touchscreen xmin=%d xmax=%d\n", xmin, xmax);
    else
    	pr_vdebug("touchscreen has no xmax: using emulator mode\n");

    if(ioctl(touchfd, EVIOCGABS(ABS_Y), &info)) {
        pr_err("cannot get ABS_Y, %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    ymin = info.minimum;
    ymax = info.maximum;
    if (ymax)
    	pr_vdebug("touchscreen ymin=%d ymax=%d\n", ymin, ymax);
    else
    	pr_vdebug("touchscreen has no ymax: using emulator mode\n");
}

static void cleanup_touch()
{
	if(touchfd != -1)
	{
		close(touchfd);
	}
}

/*****************************************************************************/

static void init_fb_server(int argc, char **argv)
{
	pr_info("Initializing server...\n");

	/* Allocate the VNC server buffer to be managed (not manipulated) by 
	 * libvncserver. */
	vncbuf = calloc(scrinfo.xres * scrinfo.yres, scrinfo.bits_per_pixel / 2);
	assert(vncbuf != NULL);

	/* Allocate the comparison buffer for detecting drawing updates from frame
	 * to frame. */
	fbbuf = calloc(scrinfo.xres * scrinfo.yres, scrinfo.bits_per_pixel / 2);
	assert(fbbuf != NULL);

	/* FIXME: This assumes scrinfo.bits_per_pixel is 16. */
	vncscr = rfbGetScreen(&argc, argv, scrinfo.xres, scrinfo.yres,
			5, /* bits per sample */
			2, /* samples per pixel */
			2  /* bytes/sample */ );

	assert(vncscr != NULL);

	vncscr->desktopName = VNC_DESKTOP_NAME;
	vncscr->frameBuffer = (char *)vncbuf;
	vncscr->alwaysShared = TRUE;
	vncscr->httpDir = NULL;
	vncscr->port = VNC_PORT;

	vncscr->kbdAddEvent = keyevent;
	vncscr->ptrAddEvent = ptrevent;

	rfbInitServer(vncscr);

	/* Mark as dirty since we haven't sent any updates at all yet. */
	rfbMarkRectAsModified(vncscr, 0, 0, scrinfo.xres, scrinfo.yres);

	/* Bit shifts */
	varblock.r_offset = scrinfo.red.offset + scrinfo.red.length - 5;
	varblock.g_offset = scrinfo.green.offset + scrinfo.green.length - 5;
	varblock.b_offset = scrinfo.blue.offset + scrinfo.blue.length - 5;
	varblock.pixels_per_int = 8 * sizeof(int) / scrinfo.bits_per_pixel;
}

/*****************************************************************************/
void injectKeyEvent(uint16_t code, uint16_t value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    gettimeofday(&ev.time,0);
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = value;
    if(write(kbdfd, &ev, sizeof(ev)) < 0)
    {
        pr_err("write event failed, %s\n", strerror(errno));
    }

    pr_vdebug("injectKey (%d, %d)\n", code , value);
}

/* device independent */
static int keysym2scancode(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
    int scancode = 0;

    int code = (int) key;
    if (code>='0' && code<='9') {
        scancode = (code & 0xF) - 1;
        if (scancode<0) scancode += 10;
        scancode += KEY_1;
    } else if (code>=0xFF50 && code<=0xFF58) {
        static const uint16_t map[] =
             {  KEY_HOME, KEY_LEFT, KEY_UP, KEY_RIGHT, KEY_DOWN,
                KEY_SOFT1, KEY_SOFT2, KEY_END, 0 };
        scancode = map[code & 0xF];
    } else if (code>=0xFFE1 && code<=0xFFEE) {
        static const uint16_t map[] =
             {  KEY_LEFTSHIFT, KEY_LEFTSHIFT,
                KEY_COMPOSE, KEY_COMPOSE,
                KEY_LEFTSHIFT, KEY_LEFTSHIFT,
                0,0,
                KEY_LEFTALT, KEY_RIGHTALT,
                0, 0, 0, 0 };
        scancode = map[code & 0xF];
    } else if ((code>='A' && code<='Z') || (code>='a' && code<='z')) {
        static const uint16_t map[] = {
                KEY_A, KEY_B, KEY_C, KEY_D, KEY_E,
                KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
                KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
                KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
                KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z };
        scancode = map[(code & 0x5F) - 'A'];
    } else {
        switch (code) {
            case 0x0003:    scancode = KEY_CENTER;      break;
            case 0x0020:    scancode = KEY_SPACE;       break;
            case 0x0023:    scancode = KEY_SHARP;       break;
            case 0x0033:    scancode = KEY_SHARP;       break;
            case 0x002C:    scancode = KEY_COMMA;       break;
            case 0x003C:    scancode = KEY_COMMA;       break;
            case 0x002E:    scancode = KEY_DOT;         break;
            case 0x003E:    scancode = KEY_DOT;         break;
            case 0x002F:    scancode = KEY_SLASH;       break;
            case 0x003F:    scancode = KEY_SLASH;       break;
            case 0x0032:    scancode = KEY_EMAIL;       break;
            case 0x0040:    scancode = KEY_EMAIL;       break;
            case 0xFF08:    scancode = KEY_BACKSPACE;   break;
            case 0xFF1B:    scancode = KEY_BACK;        break;
            case 0xFF09:    scancode = KEY_TAB;         break;
            case 0xFF0D:    scancode = KEY_ENTER;       break;
            case 0x002A:    scancode = KEY_STAR;        break;
            case 0xFFBE:    scancode = KEY_F1;        break; // F1
            case 0xFFBF:    scancode = KEY_F2;         break; // F2
            case 0xFFC0:    scancode = KEY_F3;        break; // F3
            case 0xFFC5:    scancode = KEY_F4;       break; // F8
            case 0xFFC8:    rfbShutdownServer(cl->screen,TRUE);       break; // F11            
        }
    }

    return scancode;
}

static void keyevent(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
	int scancode;

	pr_vdebug("Got keysym: %04x (down=%d)\n", (unsigned int)key, (int)down);

	if ((scancode = keysym2scancode(down, key, cl)))
	{
		injectKeyEvent(scancode, down);
	}
}

void injectTouchEvent(int down, int x, int y)
{
    struct input_event ev;

    // Re-calculate the final x and y if xmax/ymax are specified
    if (xmax) x = xmin + (x * (xmax - xmin)) / (scrinfo.xres);
    if (ymax) y = ymin + (y * (ymax - ymin)) / (scrinfo.yres);
    
    memset(&ev, 0, sizeof(ev));

    // Then send a BTN_TOUCH
    gettimeofday(&ev.time,0);
    ev.type = EV_KEY;
    ev.code = BTN_TOUCH;
    ev.value = down;
    if(write(touchfd, &ev, sizeof(ev)) < 0)
    {
        pr_err("write event failed, %s\n", strerror(errno));
    }

    // Then send the X
    gettimeofday(&ev.time,0);
    ev.type = EV_ABS;
    ev.code = ABS_X;
    ev.value = x;
    if(write(touchfd, &ev, sizeof(ev)) < 0)
    {
    	pr_err("write event failed, %s\n", strerror(errno));
    }

    // Then send the Y
    gettimeofday(&ev.time,0);
    ev.type = EV_ABS;
    ev.code = ABS_Y;
    ev.value = y;
    if(write(touchfd, &ev, sizeof(ev)) < 0)
    {
    	pr_err("write event failed, %s\n", strerror(errno));
    }

    // Finally send the SYN
    gettimeofday(&ev.time,0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if(write(touchfd, &ev, sizeof(ev)) < 0)
    {
        pr_err("write event failed, %s\n", strerror(errno));
    }

    pr_vdebug("injectTouchEvent (x=%d, y=%d, down=%d)\n", x , y, down);
}

static void ptrevent(int buttonMask, int x, int y, rfbClientPtr cl)
{
	/* Indicates either pointer movement or a pointer button press or release. The pointer is
now at (x-position, y-position), and the current state of buttons 1 to 8 are represented
by bits 0 to 7 of button-mask respectively, 0 meaning up, 1 meaning down (pressed).
On a conventional mouse, buttons 1, 2 and 3 correspond to the left, middle and right
buttons on the mouse. On a wheel mouse, each step of the wheel upwards is represented
by a press and release of button 4, and each step downwards is represented by
a press and release of button 5. 
  From: http://www.vislab.usyd.edu.au/blogs/index.php/2009/05/22/an-headerless-indexed-protocol-for-input-1?blog=61 */
	
	pr_vdebug("Got ptrevent: %04x (x=%d, y=%d)\n", buttonMask, x, y);
	if(buttonMask & 1) {
		// Simulate left mouse event as touch event
		injectTouchEvent(1, x, y);
		injectTouchEvent(0, x, y);
	}
}

static int get_framebuffer_yoffset()
{
    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) < 0) {
        pr_err("failed to get virtual screen info\n");
        return -1;
    }

    return scrinfo.yoffset;
}

#define PIXEL_FB_TO_RFB(p,r,g,b) \
	((p >> r) & 0x1f001f) | \
	(((p >> g) & 0x1f001f) << 5) | \
	(((p >> b) & 0x1f001f) << 10)

static void update_screen(void)
{
	unsigned int *f, *c, *r;
	int x, y, y_virtual;

	/* get virtual screen info */
	y_virtual = get_framebuffer_yoffset();
	if (y_virtual < 0)
		y_virtual = 0; /* no info, have to assume front buffer */

	varblock.min_x = varblock.min_y = INT_MAX;
	varblock.max_x = varblock.max_y = -1;

	f = (unsigned int *)fbmmap;        /* -> framebuffer         */
	c = (unsigned int *)fbbuf;         /* -> compare framebuffer */
	r = (unsigned int *)vncbuf;        /* -> remote framebuffer  */

	/* jump to right virtual screen */
	f += y_virtual * scrinfo.xres / varblock.pixels_per_int;

	for (y = 0; y < (int) scrinfo.yres; y++) {
		/* Compare every 2 pixels at a time, assuming that changes are
		 * likely in pairs. */
		for (x = 0; x < (int) scrinfo.xres; x += varblock.pixels_per_int) {
			unsigned int pixel = *f;

			if (pixel != *c) {
				*c = pixel; /* update compare buffer */

				/* XXX: Undo the checkered pattern to test the
				 * efficiency gain using hextile encoding. */
				if (pixel == 0x18e320e4 || pixel == 0x20e418e3)
					pixel = 0x18e318e3; /* still needed? */

				/* update remote buffer */
				*r = PIXEL_FB_TO_RFB(pixel,
						varblock.r_offset,
						varblock.g_offset,
						varblock.b_offset);

				if (x < varblock.min_x)
					varblock.min_x = x;
				else {
					if (x > varblock.max_x)
						varblock.max_x = x;
				}

				if (y < varblock.min_y)
					varblock.min_y = y;
				else if (y > varblock.max_y)
					varblock.max_y = y;
			}

			f++, c++;
			r++;
		}
	}

	if (varblock.min_x < INT_MAX) {
		if (varblock.max_x < 0)
			varblock.max_x = varblock.min_x;

		if (varblock.max_y < 0)
			varblock.max_y = varblock.min_y;

		pr_vdebug("Changed frame: %dx%d @ (%d,%d)...\n",
		  (varblock.max_x + 2) - varblock.min_x,
		  (varblock.max_y + 1) - varblock.min_y,
		  varblock.min_x, varblock.min_y);

		rfbMarkRectAsModified(vncscr, varblock.min_x, varblock.min_y,
		  varblock.max_x + 2, varblock.max_y + 1);

		rfbProcessEvents(vncscr, 10000); /* update quickly */
	}
}

void blank_framebuffer()
{
	int i, n = scrinfo.xres * scrinfo.yres / varblock.pixels_per_int;
	for (i = 0; i < n; i++) {
		((int *)vncbuf)[i] = 0;
		((int *)fbbuf)[i] = 0;
	}
}

/*****************************************************************************/

void print_usage(char **argv)
{
	pr_info("%s [-k device] [-t device] [-h]\n"
		"-k device: keyboard device node, default is %s\n"
		"-t device: touch device node, default is %s\n"
		"-h : print this help\n",
		APPNAME, KBD_DEVICE, TOUCH_DEVICE);
}

int input_finder(int max_num, const char* *patterns, char *path, int path_size)
{
	char name[128], trypath[PATH_MAX];
	const char* *keystr;
	int fd = -1, i, j;
	int device_id = -1, key_id = -1;

	for (i = 0; i < max_num; i++)
	{
		snprintf(trypath, sizeof(trypath), DEV_FMT, i);

		if ((fd = open(trypath, O_RDONLY)) < 0) {
			continue;
		}

		if(ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
			close(fd);
			continue;
		}

		close(fd);

		for (keystr = patterns, j = 0; *keystr; keystr++, j++) {
			if (strstr(name, *keystr)) {
				/* save the device matching earliest pattern */
				if (key_id < 0 || key_id > j) {
					key_id = j;
					device_id = i;
					strncpy(path, trypath, path_size);
					path[path_size - 1] = '\0';
				}
			}
		} /* for */
	}

	if (device_id >= 0)
		pr_info("Found input device %s by keyword %s\n", path,
				patterns[key_id]);

	return device_id; /* -1 if not found */
}

/* determine input device paths */
int input_search()
{
	const int max_input_num = 5;
	int rc = 0;
	if (input_finder(max_input_num, KBD_PATTERNS,
			KBD_DEVICE, sizeof KBD_DEVICE) < 0) {
		pr_vdebug("Cannot automatically find the keyboard device\n");
		rc++;
	}

	if (input_finder(max_input_num, TOUCH_PATTERNS,
			TOUCH_DEVICE, sizeof TOUCH_DEVICE) < 0) {
		pr_vdebug("Cannot automatically find the touchscreen device\n");
		rc++;
	}
	return rc;
}

void exit_cleanup(void)
{
	pr_info("Cleaning up...\n");
	cleanup_fb();
	cleanup_kbd();
	cleanup_touch();
}

void sigint_handler(int arg)
{
	if (old_sigint_handler)
		old_sigint_handler(arg);

	if (vncbuf)
		free(vncbuf);

	rfbScreenCleanup(vncscr);

	pr_err("<break> exit.\n");
	exit(-1);
}

int main(int argc, char **argv)
{
	/* attempts to auto-determine input devices first */
	input_search();

	if(argc > 1)
	{
		int i=1;
		while(i < argc)
		{
			if(*argv[i] == '-')
			{
				switch(*(argv[i] + 1))
				{
				case 'h':
					print_usage(argv);
					exit(0);
					break;
				case 'k':
					i++;
					strcpy(KBD_DEVICE, argv[i]);
					break;
				case 't':
					i++;
					strcpy(TOUCH_DEVICE, argv[i]);
					break;
				}
			}
			i++;
		}
	}

	pr_info("Initializing framebuffer device " FB_DEVICE "...\n");
	init_fb();

	if (KBD_DEVICE[0]) {
		pr_info("Initializing keyboard device %s ...\n", KBD_DEVICE);
		init_kbd();
	}

	if (TOUCH_DEVICE[0]) {
		pr_info("Initializing touch device %s ...\n", TOUCH_DEVICE);
		init_touch();
	}

	pr_info("Initializing Framebuffer VNC server:\n");
	pr_info("	width:  %d\n", (int)scrinfo.xres);
	pr_info("	height: %d\n", (int)scrinfo.yres);
	pr_info("	bpp:    %d\n", (int)scrinfo.bits_per_pixel);
	pr_info("	port:   %d\n", (int)VNC_PORT);
	init_fb_server(argc, argv);

	atexit(exit_cleanup);
	old_sigint_handler = signal(SIGINT, sigint_handler);

	/* Implement our own event loop to detect changes in the framebuffer. */
	while (1) {
		rfbClientPtr client_ptr;
		while (!vncscr->clientHead) {
			/* sleep until getting a client */
			rfbProcessEvents(vncscr, LONG_MAX);
		}

		/* refresh screen every 100 ms */
		rfbProcessEvents(vncscr, 100 * 1000 /* timeout in us */);

		/* all clients closed */
		if (!vncscr->clientHead) {
			blank_framebuffer(vncbuf);
		}

		/* scan screen if at least one client has requested */
		for (client_ptr = vncscr->clientHead; client_ptr; client_ptr = client_ptr->next)
		{
			if (!sraRgnEmpty(client_ptr->requestedRegion)) {
				update_screen();
				break;
			}
		}
	}

	return 0;
}
