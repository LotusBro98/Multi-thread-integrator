#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>

#include "ui.h"

struct timeval start;
struct timeval lastPrint;
int firstTime = true;

long getMillis(struct timeval tv)
{
	return (tv.tv_sec % 10000) * 1000 + tv.tv_usec / 1000;
}

long getMicros(struct timeval tv)
{
	return (tv.tv_sec % 100) * 1000000 + tv.tv_usec;
}


void initTiming(struct ChildAnswer* answers, int nChildren)
{
	gettimeofday(&start, NULL);

	for (int i = 0; i < nChildren; i++)
	{
		answers[i].sent = start;
		answers[i].received = start;
		answers[i].sentBack = start;
	}
}

void exitError()
{
	fprintf(stderr, "Error %d: %s\n", errno, strerror(errno));
	exit(EXIT_FAILURE);
}


void exitErrorMsg(char* description)
{
	fprintf(stderr, "%s", description);
	exit(EXIT_FAILURE);
}

void printAnswer(double left, double right, double maxDeviation, double I)
{
	fprintf(stderr, "\033M");
	fflush(stderr);
	for (int i = 0; i < 128; i++)
		fprintf(stderr, " ");
	
	fprintf(stderr,
		"\n"\
		"%7.3f\n"\
		"    /\n"\
		"    | f(x) dx = ",
		right
	);
	fflush(stderr);

	char fmt[8];
	sprintf(fmt, "%%.%dlf", (int)(-log10(maxDeviation) - 0.001) + 1);

	printf(fmt, I);
	fflush(stdout);

	fprintf(stderr, " +/- %lg\n"\
		"    /\n"\
		"%7.3f\n"\
		"\n",
		maxDeviation, left
	);
}

void printProgress(struct SegmentList segList, double left, double right, double I)
//void printProgress(struct SegmentList segList, double left, double right, double I, struct ChildAnswer* answers, int nChildren)
{
	int dots = 100;
	double unitsPerDot = (right - left) / dots;
	double x = left;
	struct UnstudiedSegment* p = segList.head->next;

	struct timeval tv;
	gettimeofday(&tv, NULL);

	if (p != segList.head && getMillis(tv) - getMillis(lastPrint) < PRINT_PERIOD_MS)
		return;
	else
		lastPrint = tv;

	if (firstTime)
		firstTime = false;
	else
	{
		fprintf(stderr, "\033M");
//		for (int i = 0; i < nChildren + 3; i++)
//			fprintf(stderr, "\033M");
		fflush(stdout);
	}

	
	while (x < right)
	{
		if (p != segList.head && x > p->left)
			fprintf(stderr, "\033[91m-\033[0m");
		else
			fprintf(stderr, "\033[93m+\033[0m");

		while (p != segList.head && p->next != segList.head && x > p->next->left)
			p = p->next;

		x += unitsPerDot;
	}

	fprintf(stderr, "  %.18lf\n", I);

//	printTimes(answers, nChildren);
}

void printTimes(struct ChildAnswer* answers, int nChildren)
{
	long minTime = getMicros(answers[0].sent);
	long maxTime = getMicros(answers[0].sentBack);

	for (int i = 0; i < nChildren; i++)
	{
		if (getMicros(answers[i].sent) < minTime)
			minTime = getMicros(answers[i].sent);

		if (getMicros(answers[i].sentBack) > maxTime)
			maxTime = getMicros(answers[i].sentBack);
	}

	int dots = 100;
	double microsPerDot = (maxTime - minTime) / (double)dots;
	double startTime = minTime;

	for (int i = 0; i < dots + 10; i++)
		printf(" ");
	printf("\n");
	
	printf("%4ld", minTime - getMicros(start));
	for (int i = 8; i < dots; i++)
		printf("-");
	printf("%4ld\n", maxTime - getMicros(start));

	for (int i = 0; i < dots + 2; i++)
		printf(" ");
	printf("\n");

	for (int n = 0; n < nChildren; n++)
	{
		if (microsPerDot != 0)
		{
			int i = 0;
			for (; startTime + i * microsPerDot < getMicros(answers[n].sent) && i < dots; i++)
				printf(" ");

			printf("\033[91m");
			for (; startTime + i * microsPerDot < getMicros(answers[n].received) && i < dots; i++)
				printf("*");
	
			printf("\033[92m");
			for (; startTime + i * microsPerDot < getMicros(answers[n].sentBack) && i < dots; i++)
				printf("*");
	
			for (i--; startTime + i * microsPerDot < maxTime && i < dots; i++)
				printf(" ");
		}

		printf("\033[0m\n");
	}
}

void parseArgs(int argc, char* argv[], double* left, double* right, int* nChildren, double* maxDeviation)
{
	if (argc == 1)
		exitErrorMsg(
"\n Usage: ./integrate <from> <to> [nChildren] [maxDeviation]\n\n"/*\
Calculates definite integral of function 'func', specified in 'libfunction.so'.\n\
'libfunction.so' is compiled from 'function.c'. To change the function, edit 'function.c', then run 'make'.\n\
All parameters except <nChildren> are of type double.\n"*/
		);
	else if (argc < 3 || argc > 6)
		exitErrorMsg("Wrong format. Type './integrate' for help.\n");

	char* endptr;
	*left  = strtod(argv[1], &endptr);
	if (errno != 0 || (unsigned)(endptr - argv[1]) != strlen(argv[1]))
		exitErrorMsg("Failed to convert 1st argument to double.\n");

	*right = strtod(argv[2], &endptr);
	if (errno != 0 || (unsigned)(endptr - argv[2]) != strlen(argv[2]))
		exitErrorMsg("Failed to convert 2nd argument to double.\n");

	if (argc >= 4)
	{
		*nChildren = strtol(argv[3], &endptr, 10);
		if (errno != 0 || (unsigned)(endptr - argv[3]) != strlen(argv[3]))
			exitErrorMsg("Failed to convert 3rd argument to int.\n");
	}
		else *nChildren = 1;

	if (argc >= 5)
	{
		*maxDeviation = strtod(argv[4], &endptr);
		if (errno != 0 || (unsigned)(endptr - argv[4]) != strlen(argv[4]))
			exitErrorMsg("Failed to convert 4th argument to double.\n");
	}
		else *maxDeviation = 0.000001;
}

void explainError(enum ErrorCode error)
{
	switch (error)
	{
		case ERR_NO_ERROR:
			fprintf(stderr, "Success.\n");
			break;
		case ERR_BEST_FINENESS_REACHED:
			fprintf(stderr, 
"\n\nBest possible fineness for type double has been reached. \
Further calculations will lead to accuracy loss or, possibly, to a deadlock. \
Please, try running the program again with bigger <maxDeviation>.\n\n");
			break;
		case ERR_OTHER:
			fprintf(stderr, "An error occured while integrating the function. Try relaunching the program.\n");
			break;
		case ERR_CHILD_DISCONNECTED:
			fprintf(stderr, "Lost connection with one of calculating processes. Relaunching the program may help.\n");
			break;
	}
}
