/*
 * testlpp.c: use the /dev/lpptest device to test IRQ handling
 *            latencies over parallel port
 *
 *      Copyright (C) 2005 Thomas Gleixner
 *
 * licensed under the GPL
 */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/io.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define LPPTEST_CHAR_MAJOR 245
#define LPPTEST_DEVICE_NAME "lpptest"

#define LPPTEST_TEST    _IOR (LPPTEST_CHAR_MAJOR, 1, unsigned long long)
#define LPPTEST_DISABLE _IOR (LPPTEST_CHAR_MAJOR, 2, unsigned long long)
#define LPPTEST_ENABLE  _IOR (LPPTEST_CHAR_MAJOR, 3, unsigned long long)

#define HIST_SIZE 10000

static int hist_total;
static unsigned long hist[HIST_SIZE];

static void hist_hit(unsigned long usecs)
{
	hist_total++;
	if (usecs >= HIST_SIZE-1)
		hist[HIST_SIZE-1]++;
	else
		hist[usecs]++;
}

static void print_hist(void)
{
	int i;

	printf("LPP latency histogram:\n");

	for (i = 0; i < HIST_SIZE; i++) {
		if (hist[i])
			printf("%3d usecs: %9ld\n", i, hist[i]);
	}
}

static inline unsigned long long int rdtsc(void)
{
	unsigned long long int x, y;
	for (;;) {
		__asm__ volatile ("rdtsc" : "=A" (x));
		__asm__ volatile ("rdtsc" : "=A" (y));
		if (y - x < 1000)
			return y;
	}
}

static unsigned long long calibrate_loop(void)
{
	unsigned long long mytime1, mytime2;

	mytime1 = rdtsc();
	usleep(500000);
	mytime2 = rdtsc();

	return (mytime2 - mytime1) * 2;
}

#define time_to_usecs(time) ((double)time*1000000.0/(double)cycles_per_sec)

#define time_to_usecs_l(time) (long)(time*1000000/cycles_per_sec)

int fd, total;
unsigned long long tim, sum_tim, min_tim = -1ULL, max_tim, cycles_per_sec;

void cleanup(int sig)
{
	ioctl (fd, LPPTEST_ENABLE, &tim);
	if (sig)
		printf("[ interrupted - exiting ]\n");
	printf("\ntotal number of responses: %d\n", total);
	printf("average reponse latency:   %.2lf usecs\n",
		time_to_usecs(sum_tim/total));
	printf("minimum latency:           %.2lf usecs\n",
			time_to_usecs(min_tim));
	printf("maximum latency:           %.2lf usecs\n",
			time_to_usecs(max_tim));
	print_hist();
	exit(0);
}

#define HZ 3000

int main (int argc, char **argv)
{
	unsigned int nr_requests = 0;

	if (argc > 2) {
		fprintf(stderr, "usage: testlpp [<nr_of_requests>]\n");
		exit(-1);
	}
	if (argc == 2)
		nr_requests = atol(argv[1]);

	if (getuid() != 0) {
		fprintf(stderr, "need to run as root!\n");
		exit(-1);
	}
	mknod("/dev/lpptest", S_IFCHR|0666, makedev(245, 1));

	fd = open("/dev/lpptest", O_RDWR);
	if (fd == -1) {
		fprintf(stderr, "could not open /dev/lpptest, your kernel doesnt have CONFIG_LPPTEST enabled?\n");
		exit(-1);
	}

	signal(SIGINT,&cleanup);

	ioctl (fd, LPPTEST_DISABLE, &tim);

	fprintf(stderr, "calibrating cycles to usecs: ");
	cycles_per_sec = calibrate_loop();
	fprintf(stderr, "%lld cycles per usec\n", cycles_per_sec/1000000);
	if (nr_requests)
		fprintf(stderr, "[max # of requests: %u]\n", nr_requests);
	fprintf(stderr, "starting %dHz test, hit Ctrl-C to stop:\n\n", HZ);

	while(1) {
		ioctl (fd, LPPTEST_TEST, &tim);
		if (tim == 0)
			printf ("No response from target.\n");
		else {
			hist_hit(time_to_usecs_l(tim));
			if (tim > max_tim) {
				printf ("new max latency: %.2lf usecs (%Ld cycles)\n", time_to_usecs(tim), tim);
				max_tim = tim;
			}
			if (tim < min_tim)
				min_tim = tim;
			total++;
			if (total == nr_requests)
				break;
			sum_tim += tim;
		}
		usleep(1000000/HZ);
	}
	cleanup(0);

	return 0;
}


