/*
 * Loopback test application
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_SYSFS_PATH	0x200
#define CSV_MAX_LINE	0x1000
#define SYSFS_MAX_INT	0x20

struct dict {
	char *name;
	int type;
};

static struct dict dict[] = {
	{"ping", 2},
	{"transfer", 3},
	{"sink", 4}
};

static int verbose = 1;

void abort()
{
	_exit(1);
}

void usage(void)
{
	fprintf(stderr, "Usage: looptest TEST SIZE ITERATIONS PATH\n\n"
	"  Run TEST for a number of ITERATIONS with operation data SIZE bytes\n"
	"  TEST may be \'ping\' \'transfer\' or \'sink\'\n"
	"  SIZE indicates the size of transfer <= greybus max payload bytes\n"
	"  ITERATIONS indicates the number of times to execute TEST at SIZE bytes\n"
	"             Note if ITERATIONS is set to zero then this utility will\n"
	"             initiate an infinite (non terminating) test and exit\n"
	"             without logging any metrics data\n"
	"  PATH indicates the sysfs path for the loopback greybus entries e.g.\n"
	"       /sys/bus/greybus/devices/endo0:1:1:1:1/\n"
	"  DEV specifies the loopback device to read raw latcency timings from e.g.\n"
	"       /dev/gb/loopback0\n"
	"Examples:\n"
	"  looptest transfer 128 10000 /sys/bus/greybus/devices/endo0:1:1:1:1/ /dev/gb/loopback0\n"
	"  looptest ping 0 128 /sys/bus/greybus/devices/endo0:1:1:1:1/ /dev/gb/loopback0\n"
	"  looptest sink 2030 32768 /sys/bus/greybus/devices/endo0:1:1:1:1/ /dev/gb/loopback0\n");
	abort();
}

int open_sysfs(const char *sys_pfx, const char *node, int flags)
{
	extern int errno;
	int fd;
	char path[MAX_SYSFS_PATH];

	snprintf(path, sizeof(path), "%s%s", sys_pfx, node);
	fd = open(path, flags);
	if (fd < 0) {
		fprintf(stderr, "unable to open %s\n", path);
		abort();
	}
	return fd;
}

int read_sysfs_int_fd(int fd, const char *sys_pfx, const char *node)
{
	char buf[SYSFS_MAX_INT];

	if (read(fd, buf, sizeof(buf)) < 0) {
		fprintf(stderr, "unable to read from %s%s %s\n", sys_pfx, node,
			strerror(errno));
		close(fd);
		abort();
	}
	return atoi(buf);
}

float read_sysfs_float_fd(int fd, const char *sys_pfx, const char *node)
{
	char buf[SYSFS_MAX_INT];

	if (read(fd, buf, sizeof(buf)) < 0) {
		fprintf(stderr, "unable to read from %s%s %s\n", sys_pfx, node,
			strerror(errno));
		close(fd);
		abort();
	}
	return atof(buf);
}

int read_sysfs_int(const char *sys_pfx, const char *node)
{
	extern int errno;
	int fd, val;

	fd = open_sysfs(sys_pfx, node, O_RDONLY);
	val = read_sysfs_int_fd(fd, sys_pfx, node);
	close(fd);
	return val;
}

float read_sysfs_float(const char *sys_pfx, const char *node)
{
	extern int errno;
	int fd;
	float val;

	fd = open_sysfs(sys_pfx, node, O_RDONLY);
	val = read_sysfs_float_fd(fd, sys_pfx, node);
	close(fd);
	return val;
}

void write_sysfs_val(const char *sys_pfx, const char *node, int val)
{
	extern int errno;
	int fd, len;
	char buf[SYSFS_MAX_INT];

	fd = open_sysfs(sys_pfx, node, O_RDWR);
	len = snprintf(buf, sizeof(buf), "%d", val);
	if (write(fd, buf, len) < 0) {
		fprintf(stderr, "unable to write to %s%s %s\n", sys_pfx, node,
			strerror(errno));
		close(fd);
		abort();
	}
	close(fd);
}

void log_csv_error(int len, int err)
{
	fprintf(stderr, "unable to write %d bytes to csv %s\n", len,
		strerror(err));
}

void log_csv(const char *test_name, int size, int iteration_max,
	     const char *sys_pfx, const char *gb_loopback_dev)
{
	extern int errno;
	char buf[CSV_MAX_LINE];
	char *date;
	int error, fd, fd_dev, len;
	float request_avg, latency_avg, throughput_avg;
	int request_min, request_max, request_jitter;
	int latency_min, latency_max, latency_jitter;
	int throughput_min, throughput_max, throughput_jitter;
	unsigned int i;
	uint32_t val;
	struct tm tm;
	time_t t;

	fd_dev = open(gb_loopback_dev, O_RDONLY);
	if (fd_dev < 0) {
		fprintf(stderr, "unable to open specified device %s\n",
			gb_loopback_dev);
		return;
	}

	/*
	 * file name will test_name_size_iteration_max.csv
	 * every time the same test with the same parameters is run we will then
	 * append to the same CSV with datestamp - representing each test
	 * dataset.
	 */
	snprintf(buf, sizeof(buf), "%s_%d_%d.csv", test_name, size,
		 iteration_max);

	/* gather data set */
	t = time(NULL);
	tm = *localtime(&t);
	error = read_sysfs_int(sys_pfx, "error");
	request_min = read_sysfs_int(sys_pfx, "requests_per_second_min");
	request_max = read_sysfs_int(sys_pfx, "requests_per_second_max");
	request_avg = read_sysfs_float(sys_pfx, "requests_per_second_avg");
	latency_min = read_sysfs_int(sys_pfx, "latency_min");
	latency_max = read_sysfs_int(sys_pfx, "latency_max");
	latency_avg = read_sysfs_float(sys_pfx, "latency_avg");
	throughput_min = read_sysfs_int(sys_pfx, "throughput_min");
	throughput_max = read_sysfs_int(sys_pfx, "throughput_max");
	throughput_avg = read_sysfs_float(sys_pfx, "throughput_avg");

	/* derive jitter */
	request_jitter = request_max - request_min;
	latency_jitter = latency_max - latency_min;
	throughput_jitter = throughput_max - throughput_min;

	fd = open(buf, O_WRONLY|O_CREAT|O_APPEND);
	if (fd < 0) {
		fprintf(stderr, "unable to open %s for appendation\n", buf);
		abort();
	}

	/* append calculated metrics to file */
	memset(buf, 0x00, sizeof(buf));
	len = snprintf(buf, sizeof(buf), "%u-%u-%u %u:%u:%u,",
		       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		       tm.tm_hour, tm.tm_min, tm.tm_sec);
	len += snprintf(&buf[len], sizeof(buf) - len,
			"%s,%u,%u,%u,%u,%u,%f,%u,%u,%u,%f,%u,%u,%u,%f,%u",
			test_name, size, iteration_max, error,
			request_min, request_max, request_avg, request_jitter,
			latency_min, latency_max, latency_avg, latency_jitter,
			throughput_min, throughput_max, throughput_avg,
			throughput_jitter);
	write(fd, buf, len);

	/* print basic metrics to stdout - requested feature add */
	printf("\n%s\n", buf);

	/* Write raw latency times to CSV  */
	for (i = 0; i < iteration_max; i++) {
		len = read(fd_dev, &val, sizeof(val));
		if (len < 0) {
			fprintf(stderr, "error reading %s %s\n",
				gb_loopback_dev, strerror(errno));
			break;
		}
		len = snprintf(buf, sizeof(buf), ",%u", val);
		if (write(fd, buf, len) != len) {
			log_csv_error(0, errno);
			break;
		}
	}
	if (write(fd, "\n", 1) < 1)
		log_csv_error(1, errno);

	/* skip printing large set to stdout just close open handles */
	close(fd_dev);
	close(fd);
}

void loopback_run(const char *test_name, int size, int iteration_max,
		  const char *sys_pfx, const char *gb_loopback_dev)
{
	char buf[MAX_SYSFS_PATH];
	char inotify_buf[0x800];
	extern int errno;
	fd_set fds;
	int test_id = 0;
	int i, percent;
	int previous, err, iteration_count;
	int fd, wd, ret;
	struct timeval tv;

	for (i = 0; i < sizeof(dict) / sizeof(struct dict); i++) {
		if (!strstr(dict[i].name, test_name))
			test_id = dict[i].type;
	}
	if (!test_id) {
		fprintf(stderr, "invalid test %s\n", test_name);
		usage();
		return;
	}

	/* Terminate any currently running test */
	write_sysfs_val(sys_pfx, "type", 0);

	/* Set parameter for no wait between messages */
	write_sysfs_val(sys_pfx, "ms_wait", 0);

	/* Set operation size */
	write_sysfs_val(sys_pfx, "size", size);

	/* Set iterations */
	write_sysfs_val(sys_pfx, "iteration_max", iteration_max);

	/* Initiate by setting loopback operation type */
	write_sysfs_val(sys_pfx, "type", test_id);
	sleep(1);

	if (iteration_max == 0) {
		printf("Infinite test initiated CSV won't be logged\n");
		return;
	}

	/* Setup for inotify on the sysfs entry */
	fd = inotify_init();
	if (fd < 0) {
		fprintf(stderr, "inotify_init fail %s\n", strerror(errno));
		abort();
	}
	snprintf(buf, sizeof(buf), "%s%s", sys_pfx, "iteration_count");
	wd = inotify_add_watch(fd, buf, IN_MODIFY);
	if (wd < 0) {
		fprintf(stderr, "inotify_add_watch %s fail %s\n",
			buf, strerror(errno));
		close(fd);
		abort();
	}

	previous = 0;
	err = 0;
	while (1) {
		/* Wait for change */
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		ret = select(fd + 1, &fds, NULL, NULL, &tv);

		if (ret > 0) {
			if (!FD_ISSET(fd, &fds)) {
				fprintf(stderr, "error - FD_ISSET fd=%d flase!\n",
					fd);
				break;
			}
			/* Read to clear the event */
			ret = read(fd, inotify_buf, sizeof(inotify_buf));
		}

		/* Grab the data */
		iteration_count = read_sysfs_int(sys_pfx, "iteration_count");

		/* Validate data value is different */
		if (previous == iteration_count) {
			err = 1;
			break;
		} else if (iteration_count == iteration_max) {
			break;
		}
		previous = iteration_count;
		if (verbose) {
			printf("%02d%% complete %d of %d\r",
				100 * iteration_count / iteration_max,
				iteration_count, iteration_max);
		}
	}
	inotify_rm_watch(fd, wd);
	close(fd);

	if (err)
		printf("\nError executing test\n");
	else
		log_csv(test_name, size, iteration_max, sys_pfx,
			gb_loopback_dev);
}

int main(int argc, char *argv[])
{
	if (argc != 6)
		usage();
	loopback_run(argv[1], atoi(argv[2]), atoi(argv[3]), argv[4], argv[5]);
	return 0;
}
