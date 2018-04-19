#ifndef GENERAL_H
#define GENERAL_H

#include <sys/time.h>

struct CalcRequest
{
	double left;
	double right;
	int nSegments;
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

#endif
