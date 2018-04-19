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

#include "ui.h"
#include "list.h"
#include "general.h"

extern double func(double x);

#define SEGMENTS_PER_PROCESS 0x10000

double startSegLen;

struct Connection
{
	int rd;
	int wr;
	int waiting;
	int closed;
};


void createChildren(struct Connection* *con, int nChildren);

void calcSums(double (*func)(double x), double left, double right, double* s, double* S, int nSegments, double dens);
void childCalcSums(int rd, int wr);

double parentIntegrate(struct Connection* con, int nChildren, double left, double right, double maxDeviation);


int main(int argc, char* argv[])
{
	double left, right, maxDeviation;
	int nChildren;
		
	parseArgs(argc, argv, &left, &right, &nChildren, &maxDeviation);

	startSegLen = (right - left);

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

int getNSegments(double len, double startLen)
{
	int pow = log2(startLen / len);

	if (pow < 8)
		pow = 8;

	if (pow > 30)
		pow = 30;

	return 0x1 << pow;	
}

void makeRequest(struct UnstudiedSegment* seg, struct CalcRequest* rq, int child, double dens)
{
	rq->left = seg->left;
	rq->right = seg->right;
	rq->dens = dens;

//	rq->nSegments = SEGMENTS_PER_PROCESS;
	rq->nSegments = getNSegments(rq->right - rq->left, startSegLen);

	gettimeofday(&(rq->sent), NULL);

	seg->child = child;
}

int handleSegmentData(
	struct SegmentList segList, 
	struct ChildAnswer* ans, 
	struct CalcRequest* rq,
	int child,
	double* I, 
	double dens)
{
	struct UnstudiedSegment* seg = getSeg(segList, child);

	if (ans->eps < dens)
	{
		seg->S = ans->S;
		*I += removeSeg(seg);

		seg = getSeg(segList, -1);
		if (seg == NULL)
			return false;

		makeRequest(seg, rq, child, dens);
		return true;
	}
	else
	{
		split(seg);

		seg = getSeg(segList, -1);
		makeRequest(seg, rq, child, dens);
		return true;
	}
}

double parentIntegrate(struct Connection* con, int nChildren, double left, double right, double maxDeviation)
{
	double dens = maxDeviation / (right - left);
	struct CalcRequest rq;
	struct ChildAnswer ans;
	struct SegmentList segList = initList(left, right, nChildren);
	struct UnstudiedSegment* seg;
	double I = 0;
	
	fd_set rd;

	while (!isEmpty(segList))
	{
		for (int i = 0; i < nChildren; i++)
		if (!(con[i].closed) && con[i].waiting && (seg = getSeg(segList, -1)) != NULL)
		{
			makeRequest(seg, &rq, i, dens);
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
			if (!(con[i].closed) && !(con[i].waiting))
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

				if(handleSegmentData(segList, &ans, &rq, i, &I, dens))
				{
					write(con[i].wr, &rq, sizeof(rq));
					if (errno != 0)
					{
						con[i].closed = true;
						fprintf(stderr, "Lost connection with child %d: %s (%d)\n", i, strerror(errno), errno);
					}
				}
				else
					con[i].waiting = true;
			}

		printProgress(segList, left, right, I);
	}

	destroyList(segList);

	for (int i = 0; i < nChildren; i++)
	{
		close(con[i].rd);
		close(con[i].wr);
		free(con);
	}

	return I;
}

void childCalcSums(int rd, int wr)
{
	struct CalcRequest rq;
	struct ChildAnswer ans;

	while (read(rd, &rq, sizeof(rq)))
	{
		gettimeofday(&ans.received, NULL);
		calcSums(func, rq.left, rq.right, &(ans.S), &(ans.eps), rq.nSegments, rq.dens);
		gettimeofday(&ans.sentBack, NULL);

		write(wr, &ans, sizeof(ans));
		gettimeofday(&ans.sent, NULL);
	}
}


void calcSums(double (*func)(double x), double left, double right, double* I, double* eps, int nSegments, double dens)
{
	double fleft = func(left), fright = func(right);
	double f1, f2 = 0; 
	double x;

	double DI = 0;
	double epsCur = 0;
	double maxEps = dens * nSegments;

	for (int i = 1; i <= nSegments; i++)
	{
		f1 = f2;
		x = left + i * (right - left) / nSegments;
		f2 = func(x) - fleft - i * (fright - fleft) / nSegments; 

		DI += (f1 + f2) / 2;
		epsCur += fabs(f1 - f2);

		if (epsCur > maxEps)
			break;
	}

	*eps = epsCur / nSegments;
	*I = DI / nSegments + (fright + fleft) / 2;
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



