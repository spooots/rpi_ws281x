/*
 * main.c
 *
 * Copyright (c) 2014 Jeremy Garff <jer @ jers.net>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 *     1.  Redistributions of source code must retain the above copyright notice, this list of
 *         conditions and the following disclaimer.
 *     2.  Redistributions in binary form must reproduce the above copyright notice, this list
 *         of conditions and the following disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *     3.  Neither the name of the owner nor the names of its contributors may be used to endorse
 *         or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


static char VERSION[] = "XX.YY.ZZ";

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdarg.h>
#include <getopt.h>


#include "clk.h"
#include "gpio.h"
#include "dma.h"
#include "pwm.h"
#include "version.h"

#include "ws2811.h"


#define ARRAY_SIZE(stuff)       (sizeof(stuff) / sizeof(stuff[0]))

// defaults for cmdline options
#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN                18
#define DMA                     10

#define STRIP_TYPE              WS2811_STRIP_GBR		// WS2812/SK6812RGB integrated chip+leds    // rpi: 0x00BBGGRR

#define WIDTH                   8
#define HEIGHT                  4
#define LED_COUNT               (WIDTH * HEIGHT)

int width = WIDTH;
int height = HEIGHT;
int led_count = LED_COUNT;

int clear_on_exit = 0;

int enableTest = 0;

ws2811_t ledstring =
{
    .freq = TARGET_FREQ,
    .dmanum = DMA,
    .channel =
    {
        [0] =
        {
            .gpionum = GPIO_PIN,
            .count = LED_COUNT,
            .invert = 0,
            .brightness = 255,
            .strip_type = STRIP_TYPE,
        },
        [1] =
        {
            .gpionum = 0,
            .count = 0,
            .invert = 0,
            .brightness = 0,
        },
    },
};

#define INDEX_RED 		0
#define INDEX_GREEN 	8
#define INDEX_BLUE 	   16

uint8_t Red = 0;
uint8_t Green = 0;
uint8_t Blue = 0;

ws2811_led_t *matrix;

ws2811_led_t getColorValue(uint8_t red, uint8_t green, uint8_t blue)
{
	return (red << INDEX_RED) | (green << INDEX_GREEN) | (blue << INDEX_BLUE);
}

int setLEDValue(ws2811_led_t);

static void ctrl_c_handler(int signum)
{
	(void)(signum);
	setLEDValue(getColorValue(0,0,0));
	ws2811_render(&ledstring);
	ws2811_fini(&ledstring);
	exit(-1);
}

static void setup_handlers(void)
{
    struct sigaction sa =
    {
        .sa_handler = ctrl_c_handler,
    };

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

void printUsage(char* app)
{
	fprintf(stderr, "%s version %s\n", app, VERSION);
	fprintf(stderr, "Usage: %s \n"
		"-h (--help)    - this information\n"
		"-r (--red)   	- set red value [0-255]\n"
		"-g (--green)   - set green value [0-255]\n"
		"-b (--blue)  	- set blue value [0-255]\n"
		"-o (--off)     - switch off display\n"
		"-c (--clear)   - clear on exit\n"
		"-d (--dma)     - select DMA channel\n"
		"-x (--width)   - set width\n"
		"-y (--height)  - set height\n"
		, app);
}


void parseargs(int argc, char **argv, ws2811_t *ws2811)
{
	int index;
	int c;

	static struct option longopts[] =
	{
		{"help", no_argument, 0, 'h'},
		{"red", required_argument, 0, 'r'},
		{"green", required_argument, 0, 'g'},
		{"blue", required_argument, 0, 'b'},
		{"off", no_argument, 0, 'o'},
		{"clear", no_argument, 0, 'c'},
		{"dma", required_argument, 0, 'd'},
		{"width", required_argument, 0, 'x'},
		{"height", required_argument, 0, 'y'},
		{"test", no_argument, 0, 't'},
		{0, 0, 0, 0}
	};

	while (1)
	{
		index = 0;
		c = getopt_long(argc, argv, "tr:g:b:ohcd:x:y:", longopts, &index);

		if (c == -1)
			break;

		switch (c)
		{
		case 'h':
			printUsage(argv[0]);
			exit(-1);

		case 't':
			enableTest = 1;
			break;

		case 'r': {
			if (optarg) {
				uint8_t value = atoi(optarg);
				if (value > 255 || value < 0) exit(-1);
				Red = value;
			}
			else
			{
				Red = 0;
			}
		} break;

		case 'g': {
			if (optarg) {
				uint8_t value = atoi(optarg);
				if (value > 255 || value < 0) exit(-1);
				Green = value;
			}
			else
			{
				Green = 0;
			}
		} break;

		case 'b': {
			if (optarg) {
				uint8_t value = atoi(optarg);
				if (value > 255 || value < 0) exit(-1);
				Blue = value;
			}
			else
			{
				Blue = 0;
			}
		} break;
		case 'c':
			clear_on_exit = 1;
			break;

		case 'd':
			if (optarg) {
				int dma = atoi(optarg);
				if (dma < 14) {
					ws2811->dmanum = dma;
				} else {
					printf ("invalid dma %d\n", dma);
					exit (-1);
				}
			}
			break;

		case 'y':
			if (optarg) {
				height = atoi(optarg);
				if (height > 0) {
					ws2811->channel[0].count = height * width;
				} else {
					printf ("invalid height %d\n", height);
					exit (-1);
				}
			}
			break;

		case 'x':
			if (optarg) {
				width = atoi(optarg);
				if (width > 0) {
					ws2811->channel[0].count = height * width;
				} else {
					printf ("invalid width %d\n", width);
					exit (-1);
				}
			}
			break;

		case 'v':
			fprintf(stderr, "%s version %s\n", argv[0], VERSION);
			exit(0);

		default:
			printUsage(argv[0]);
			exit(-1);
		}
	}
}

int setNextLED(ws2811_led_t value)
{
	ledstring.channel[0].leds[0] = value;
}

int setLEDValue(ws2811_led_t value)
{
	for (int i = 0; i < (width * height); ++i)
	{
		ledstring.channel[0].leds[i] = value;
	}
	return 0;
}

void runTest()
{
	for (uint8_t b = 10; b < 50; ++b)
	{
		for (uint8_t g = 10; g < 50; ++g)
		{
			for ( uint8_t r = 10; r < 50; ++r)
			{
				// setLEDValue(getColorValue(r,g,b));
				setNextLED(getColorValue(r,g,b));
				ws2811_render(&ledstring);
				// usleep(10);
			}
		}
	}
	setLEDValue(getColorValue(0,0,0));
}


int main(int argc, char *argv[])
{
	sprintf(VERSION, "%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);

	parseargs(argc, argv, &ledstring);

    setup_handlers();

	ws2811_init(&ledstring);

	if (enableTest)
	{
		runTest();
		exit(0);
	}

	setLEDValue(getColorValue(Red, Green, Blue));
	ws2811_render(&ledstring);

	ws2811_fini(&ledstring);
	return 0;
}
