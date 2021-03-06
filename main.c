/*
 * Greybus Simulator
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <usbg/usbg.h>

#include "gbsim.h"

int bbb_backend = 0;
int i2c_adapter = 0;
int uart_portno = 0;
int uart_count = 0;
char *hotplug_basedir;
int verbose = 0;

static usbg_state *s;
static usbg_gadget *g;

static struct sigaction sigact;

struct gbsim_info info;

static void cleanup(void)
{
	printf("cleaning up\n");
	sigemptyset(&sigact.sa_mask);

	uart_cleanup();
	gadget_cleanup(s, g);
	functionfs_cleanup();
	svc_exit();
}

static void signal_handler(int sig)
{
	if (sig == SIGINT || sig == SIGHUP || sig == SIGTERM)
		cleanup();
}

static void signals_init(void)
{
	sigact.sa_handler = signal_handler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, (struct sigaction *)NULL);
	sigaction(SIGHUP, &sigact, (struct sigaction *)NULL);
	sigaction(SIGTERM, &sigact, (struct sigaction *)NULL);
}

int main(int argc, char *argv[])
{
	int ret = -EINVAL;
	int o;

	while ((o = getopt(argc, argv, ":bh:i:u:U:v")) != -1) {
		switch (o) {
		case 'b':
			bbb_backend = 1;
			printf("bbb_backend %d\n", bbb_backend);
			break;
		case 'h':
			hotplug_basedir = optarg;
			printf("hotplug_basedir %s\n", hotplug_basedir);
			break;
		case 'i':
			i2c_adapter = atoi(optarg);
			printf("i2c_adapter %d\n", i2c_adapter);
			break;
		case 'u':
			uart_portno = atoi(optarg);
			printf("uart_portno %d\n", uart_portno);
			break;
		case 'U':
			uart_count = atoi(optarg);
			printf("uart_count %d\n", uart_count);
			break;
		case 'v':
			verbose = 1;
			printf("verbose %d\n", verbose);
			break;
		case ':':
			if (optopt == 'i')
				gbsim_error("i2c_adapter required\n");
			else if (optopt == 'h')
				gbsim_error("hotplug_basedir required\n");
			else if (optopt == 'u')
				gbsim_error("uart_portno required\n");
			else if (optopt == 'U')
				gbsim_error("uart_count required\n");
			else
				gbsim_error("-%c requires an argument\n",
					optopt);
			return 1;
		case '?':
			if (isprint(optopt))
				gbsim_error("unknown option -%c'\n", optopt);
			else
				gbsim_error("unknown option character 0x%02x\n",
					optopt);
			return 1;
		default:
			abort();
		}
	}

	if (!hotplug_basedir) {
		gbsim_error("hotplug directory not specified, aborting\n");
		return 1;
	}

	signals_init();

	TAILQ_INIT(&info.cports);

	ret = gadget_create(&s, &g);
	if (ret < 0)
		goto out;

	ret = functionfs_init();
	if (ret < 0)
		goto out;

	ret = gadget_enable(g);
	if (ret < 0)
		goto out;

	/* Protocol handlers */
	svc_init();
	gpio_init();
	i2c_init();
	i2s_init();
	uart_init();
	sdio_init();
	loopback_init();

	ret = functionfs_loop();

out:
	return ret;
}

