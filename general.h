#ifndef GENERAL_H
#define GENERAL_H

#include <sys/time.h>

struct CalcRequest
{
	double left;
	double right;
	double dens;

	struct timeval sent;
};

struct ChildAnswer
{
	double S;
	double eps;

	struct timeval sent;
	struct timeval received;
	struct timeval sentBack;
};

#define true 1
#define false 0

/*
#define MAX_SEGMENTS_PER_PROCESS 0x10000000
#define START_UNSTUDIED_SEGMENTS 0x100
#define START_LITTLE_SEGMENTS 0x10
*/

#endif
