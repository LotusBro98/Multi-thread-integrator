#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include <sys/select.h>
#include <wait.h>
#include <sched.h>
#include <math.h>
#include <fcntl.h>

#include "ui.h"
#include "list.h"
#include "general.h"

extern double func(double x);

struct Connection
{
	int rd;
	int wr;
	int waiting;
	int closed;
};


void createChildren(struct Connection* *con, int nChildren);

void calcSums(double (*func)(double x), double left, double right, double* s, double* S, double dens);
void childCalcSums(int rd, int wr);

double parentIntegrate(struct Connection* con, int nChildren, double left, double right, double maxDeviation);


int main(int argc, char* argv[])
{
	double left, right, maxDeviation;
	int nChildren;
	
//	cpu_set_t set;
//	CPU_ZERO(&set);
//	CPU_SET(getpid() % 4, &set);
//	sched_setaffinity(0, sizeof(set), &set);

	parseArgs(argc, argv, &left, &right, &nChildren, &maxDeviation);

	struct Connection* con;
	createChildren(&con, nChildren);

	double I = parentIntegrate(con, nChildren, left, right, maxDeviation);
	if (errno == 0)
		printAnswer(left, right, maxDeviation, I);
	else
		fprintf(stderr, "An error occured while integrating the function. Try relaunching the program.\n");

	for (int i = 0; i < nChildren; i++)
		wait(NULL);

	return 0;
}

void makeRequest(struct UnstudiedSegment* seg, struct CalcRequest* rq, int child, double dens)
{
	rq->left = seg->left;
	rq->right = seg->right;
	rq->dens = dens;

	gettimeofday(&(rq->sent), NULL);

	seg->child = child;
}

void sendRequest(struct Connection* con, struct UnstudiedSegment* seg, int child, double dens)
{
	struct CalcRequest rq;
	makeRequest(seg, &rq, -(child + 1), dens);
	write(con[child].wr, &rq, sizeof(rq));
	if (errno != 0)
	{
		con[child].closed = true;
		fprintf(stderr, "Lost connection with child %d: %s (%d)\n", child, strerror(errno), errno);
	}
}

void handleSegmentData(
	struct Connection* con,
	struct SegmentList segList, 
	struct ChildAnswer* ans, 
	int child,
	double* I, 
	double dens)
{
	struct UnstudiedSegment* seg = getSeg(segList, child + 1);

	if (ans->eps < dens)
	{
		seg->S = ans->S;
		*I += removeSeg(seg);
	}
	else
	{
		split(seg);
	}

	seg = getSeg(segList, -(child + 1));
	if (seg == NULL)
	{
		seg = getSeg(segList, 0);
		if (seg == NULL)
		{
			con[child].waiting = true;
			return;
		}
		
		sendRequest(con, seg, child, dens);
	}

	seg->child = child + 1;

	seg = getSeg(segList, 0);
	if (seg != NULL)
		sendRequest(con, seg, child, dens);
}

double parentIntegrate(struct Connection* con, int nChildren, double left, double right, double maxDeviation)
{
	double dens = maxDeviation / (right - left);
	struct CalcRequest rq;
	struct ChildAnswer ans;
	struct SegmentList segList = initList(left, right);
	struct UnstudiedSegment* seg;
	double I = 0;
	
	fd_set rd;

	while (!isEmpty(segList))
	{
		for (int i = 0; i < nChildren; i++)
			if (!(con[i].closed) && con[i].waiting && (seg = getSeg(segList, 0)) != NULL)
			{
				makeRequest(seg, &rq, i + 1, dens);
				write(con[i].wr, &rq, sizeof(rq));
				if (errno != 0)
				{
					fprintf(stderr, "Lost connection with child %d: %s (%d)\n", i, strerror(errno), errno);
					errno = 0;
					con[i].closed = true;
				}
				con[i].waiting = false;
			}
		
		int closed = true;
		for (int i = 0; i < nChildren; i++)
			if (!(con[i].closed))
			{
				closed = false;
				break;
			}

		if (closed)
		{
			errno = EFAULT;
			break;
		}

		FD_ZERO(&rd);
		for (int i = 0; i < nChildren; i++)
			if (!(con[i].closed))
				FD_SET(con[i].rd, &rd);

		select(nChildren * 2 + 10, &rd, NULL, NULL, NULL);

		for (int i = 0; i < nChildren; i++)
			if (FD_ISSET(con[i].rd, &rd))
			{
				if (read(con[i].rd, &ans, sizeof(ans)) == 0)
				{
					getSeg(segList, i)->child = -1;
					con[i].closed = true;
					fprintf(stderr, "Lost connection with child %d.\n", i);
					continue;
				}

				handleSegmentData(con, segList, &ans, i, &I, dens);
			}

		printProgress(segList, left, right, I);
	}

	destroyList(segList);

	for (int i = 0; i < nChildren; i++)
	{
		close(con[i].rd);
		close(con[i].wr);
	}
	free(con);

	return I;
}

void childCalcSums(int rd, int wr)
{
	struct CalcRequest rq;
	struct ChildAnswer ans;

//	cpu_set_t set;
//	CPU_ZERO(&set);
//	CPU_SET(getpid() % 4, &set);
//	sched_setaffinity(0, sizeof(set), &set);

	while (read(rd, &rq, sizeof(rq)))
	{
		gettimeofday(&ans.received, NULL);
		calcSums(func, rq.left, rq.right, &(ans.S), &(ans.eps), rq.dens);
		gettimeofday(&ans.sentBack, NULL);

		write(wr, &ans, sizeof(ans));
		gettimeofday(&ans.sent, NULL);
	}
}

void calcSums(double (*func)(double x), double left, double right, double* I, double* eps, double dens)
{
	const int nSegments = 0x10000;
	const int nSubSegments = 0x10;
	
	double DI = 0;
	double epsCur = 0;
	double maxEps = dens * nSegments * nSubSegments;

	for (int n = 0; n < nSegments; n++)
	{
		double l = left +  n      * (right - left) / nSegments;
		double r = left + (n + 1) * (right - left) / nSegments;

		double dI = 0;
		double fleft = func(l), fright = func(r);
		double f1, f2 = 0; 
		double x;
	
		for (int i = 1; i <= nSubSegments; i++)
		{
			f1 = f2;
			x = l + i * (r - l) / nSubSegments;
			f2 = func(x) - fleft - i * (fright - fleft) / nSubSegments; 
	
			dI += (f1 + f2) / 2;
			epsCur += fabs(f1 - f2);
	
			if (epsCur > maxEps)
				break;
		}

		DI += dI / nSubSegments + (fright + fleft) / 2;
	}

	*eps = epsCur / (nSegments * nSubSegments);
	*I = DI / nSegments * (right - left);
}

void createChildren(struct Connection* *conp, int nChildren)
{
	int childPipes[2];
	int pipefd[2];

	*conp = malloc(sizeof(struct Connection) * nChildren);
	if (conp == NULL)
		exitErrorMsg("Failed to allocate memory.\n");

	struct Connection* con = *conp;

	for (int i = 0; i < nChildren; i++)
	{
		pipe(pipefd);
		con[i].wr = pipefd[1];
		childPipes[0] = pipefd[0];
		
		pipe(pipefd);
		con[i].rd = pipefd[0];
		childPipes[1] = pipefd[1];

		con[i].closed = false;
		con[i].waiting = true;

		if (fork() == 0)
		{
			for (; i >= 0; i--) {
				close(con[i].rd);
				close(con[i].wr);
			}
			free(con);

			childCalcSums(childPipes[0], childPipes[1]);
			exit(EXIT_SUCCESS);
		}
		
		if (errno != 0)
		{
			kill(0, SIGTERM);
			exitErrorMsg("Failed to create new child process.\n");
		}
		close(childPipes[0]);
		close(childPipes[1]);
	}
}



