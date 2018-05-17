#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <signal.h>
#include <sys/select.h>
#include <wait.h>
#include <sched.h>
#include <math.h>
#include <fcntl.h>

#include "ui.h"
#include "list.h"
#include "general.h"
#include "cpuconf.h"


#define BEST_FINENESS 1e-12

enum requestOrder { RQ_FIRST, RQ_LAST };

inline double f(double x)
{
	return 4 * x * x * x;
}

struct Connection
{
	int rd;
	int wr;
	int waiting;
	int closed;
};


void createChildren(struct Connection* *con, int nChildren);
void destroyChildren(struct Connection* con, int nChildren);

void calcSums(double left, double right, double* I, double* eps, enum ErrorCode* error);
void childCalcSums(int rd, int wr, int child);

double parentIntegrate(struct Connection* con, int nChildren, double left, double right, double maxDeviation, enum ErrorCode* error);


int main(int argc, char* argv[])
{
	double left, right, maxDeviation;
	int nChildren;
	enum ErrorCode error;

	parseArgs(argc, argv, &left, &right, &nChildren, &maxDeviation);

	struct Connection* con;
	createChildren(&con, nChildren);

	double I = parentIntegrate(con, nChildren, left, right, maxDeviation, &error);
	if (error == ERR_NO_ERROR)
		printAnswer(left, right, maxDeviation, I);
	else
		explainError(error);

	destroyChildren(con, nChildren);

	return 0;
}

void makeRequest(struct UnstudiedSegment* seg, struct CalcRequest* rq, int child, enum requestOrder order, double dens)
{
	rq->left = seg->left;
	rq->right = seg->right;
	rq->dens = dens;

	gettimeofday(&(rq->sent), NULL);

	if (order == RQ_FIRST)
		seg->child = child + 1;
	else
		seg->child = -(child + 1);
}

void closeChild(struct Connection* con, struct SegmentList segList, int child)
{
	struct UnstudiedSegment* seg;

	seg = getSeg(segList, child + 1);
	if (seg != NULL)
		seg->child = 0;

	getSeg(segList, -(child + 1))->child = 0;
	if (seg != NULL)
		seg->child = 0;

	con[child].closed = true;
	//fprintf(stderr, "Lost connection with child %d: %s (%d)\n", child, strerror(errno), errno);
	fprintf(stderr, "Lost connection with child %d\n", child);
}

void sendRequest
(struct Connection* con, struct SegmentList segList, struct UnstudiedSegment* seg, 
int child, enum requestOrder order, double dens, enum ErrorCode* error)
{
	struct CalcRequest rq;
	int bytesWritten;

	makeRequest(seg, &rq, child, order, dens);
	bytesWritten = write(con[child].wr, &rq, sizeof(rq));
	if (errno != 0 || bytesWritten != sizeof(rq))
	{
		closeChild(con, segList, child);
		errno = 0;
		*error = ERR_CHILD_DISCONNECTED;
	}

	con[child].waiting = false;
}

void handleSegmentData
(struct Connection* con, struct SegmentList segList, struct ChildAnswer* ans, int child, double* I, double dens, enum ErrorCode* error)
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
	if (seg != NULL)
		seg->child = child + 1;
	else
	{
		seg = getSeg(segList, 0);
		if (seg == NULL)
		{
			con[child].waiting = true;
			return;
		}
		
		sendRequest(con, segList, seg, child, RQ_FIRST, dens, error);
	}

	seg = getSeg(segList, 0);
	if (seg != NULL)
		sendRequest(con, segList, seg, child, RQ_LAST, dens, error);
}

int isClosed(struct Connection* con, int nChildren)
{
	for (int i = 0; i < nChildren; i++)
		if (!con[i].closed)
			return false;
	return true;
}

double parentIntegrate(struct Connection* con, int nChildren, double left, double right, double maxDeviation, enum ErrorCode* error)
{
	double dens = maxDeviation / (right - left);
	struct ChildAnswer ans;
	struct SegmentList segList = initList(left, right);
	struct UnstudiedSegment* seg;
	double I = 0;
	fd_set rd;

	while (!isEmpty(segList))
	{
		for (int i = 0; i < nChildren; i++)
			if (!(con[i].closed) && con[i].waiting && (seg = getSeg(segList, 0)) != NULL)
				sendRequest(con, segList, seg, i, RQ_FIRST, dens, error);
		
		if (isClosed(con, nChildren))
			*error = ERR_OTHER;

		if (*error != ERR_NO_ERROR)	break;

		FD_ZERO(&rd);
		for (int i = 0; i < nChildren; i++)
			if (!(con[i].closed))
				FD_SET(con[i].rd, &rd);

		select(nChildren * 2 + 10, &rd, NULL, NULL, NULL);

		for (int i = 0; i < nChildren; i++)
			if (FD_ISSET(con[i].rd, &rd))
			{
				if (read(con[i].rd, &ans, sizeof(ans)) == 0 || errno != 0 || ans.error != ERR_NO_ERROR)
				{
					closeChild(con, segList, i);
					if (ans.error == ERR_NO_ERROR)
						*error = ERR_CHILD_DISCONNECTED;
					else
						*error = ans.error;
					break;
				}

				handleSegmentData(con, segList, &ans, i, &I, dens, error);
				if (*error != ERR_NO_ERROR) break;
			}

		if (*error != ERR_NO_ERROR) break;
		printProgress(segList, left, right, I);
	}

	destroyList(segList);

	return I;
}

void attachChildToCPU(int child)
{
	int cpu = getCPUForChild(child);
	destroyCPUData();

	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	sched_setaffinity(0, sizeof(set), &set);
	if (errno != 0)
	{
		fprintf(stderr, "Failed to attach child %d to CPU %d: %s (%d)\n", child, cpu, strerror(errno), errno);
		errno = 0;
	}
}

void childCalcSums(int rd, int wr, int child)
{
	struct CalcRequest rq;
	struct ChildAnswer ans;
	int bytesWritten;
	int bytesRead;
	
	attachChildToCPU(child);

	while ((bytesRead = read(rd, &rq, sizeof(rq))))
	{
		if (bytesRead != sizeof(rq))
			break;

		gettimeofday(&ans.received, NULL);
		calcSums(rq.left, rq.right, &(ans.S), &(ans.eps), &(ans.error));
		gettimeofday(&ans.sentBack, NULL);

		bytesWritten = write(wr, &ans, sizeof(ans));
		gettimeofday(&ans.sent, NULL);
		if (errno != 0 || bytesWritten != sizeof(ans))
			break;
	}
}

void calcSums(double left, double right, double* I, double* eps, enum ErrorCode* error)
{
	const int nSegments = 0x1000;
	const int nSubSegments = 0x100;
	
	if ((right - left) / nSegments / nSubSegments < BEST_FINENESS)
	{
		*error = ERR_BEST_FINENESS_REACHED;
		return;
	}

	double DI = 0;
	double epsCur = 0;

	for (register int n = 0; n < nSegments; n++)
	{
		double l = left +  n      * (right - left) / nSegments;
		double r = left + (n + 1) * (right - left) / nSegments;

		register double dI = 0;
		register double dEps = 0;
		double fleft = f(l), fright = f(r);
		register double f1, f2 = 0; 
		register double x;
	
		/*
		for (register int i = 1; i <= nSubSegments; i++)
		{
			f1 = f2;
			x = l + i * (r - l) / nSubSegments;
			f2 = f(x) - fleft - i * (fright - fleft) / nSubSegments; 
	
			dI += (f1 + f2) / 2;
			dEps += f1 > f2 ? f1 - f2 : f2 - f1;
		}
		*/

		double dt = 1.0 / nSubSegments;
		for (register double t = dt; t <= 1; t += dt)
		{
			f1 = f2;

			x = r;
			x -= l;
			x *= t;
			x += l;

			f2 = fleft;
			f2 -= fright;
			f2 *= t;
			f2 -= fleft;
			f2 += f(x);

			dI += f1;
			dI += f2;

			dEps += f1 > f2 ? f1 - f2 : f2 - f1;
		}

		dI /= 2;

		DI += dI / nSubSegments + (fright + fleft) / 2;
		epsCur += dEps / nSubSegments;
	}

	*eps = epsCur / (nSegments);
	*I = DI / nSegments * (right - left);
	*error = ERR_NO_ERROR;
}

void createChildren(struct Connection* *conp, int nChildren)
{
	int childPipes[2];
	int pipefd[2];
	int code;

	initCPUData();

	*conp = malloc(sizeof(struct Connection) * nChildren);
	if (conp == NULL)
		exitErrorMsg("Failed to allocate memory.\n");

	struct Connection* con = *conp;

	for (int i = 0; i < nChildren; i++)
	{
		code = pipe(pipefd);
		con[i].wr = pipefd[1];
		childPipes[0] = pipefd[0];
		
		code = pipe(pipefd);
		con[i].rd = pipefd[0];
		childPipes[1] = pipefd[1];

		con[i].closed = false;
		con[i].waiting = true;

		if (code != 0)
		{
			fprintf(stderr, "Failed to create pipes.\n");
			kill(0, SIGTERM);
		}

		if (fork() == 0)
		{
			int child = i;
			for (; i >= 0; i--) {
				close(con[i].rd);
				close(con[i].wr);
			}
			free(con);

			childCalcSums(childPipes[0], childPipes[1], child);
			exit(EXIT_SUCCESS);
		}
		
		if (errno != 0)
		{
			fprintf(stderr, "Failed to create new child process.\n");
			kill(0, SIGTERM);
		}
		close(childPipes[0]);
		close(childPipes[1]);
	}
}

void destroyChildren(struct Connection* con, int nChildren)
{
	for (int i = 0; i < nChildren; i++)
	{
		close(con[i].rd);
		close(con[i].wr);
	}
	free(con);
	
	for (int i = 0; i < nChildren; i++)
		wait(NULL);
}

