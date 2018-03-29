#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <poll.h>
#include <wait.h>
#include <sched.h>

extern double func(double x);

struct CalcRequest
{
	double left;
	double right;
	int nSegments;
};

struct ChildAnswer
{
	double lowSum;
	double highSum;
};


#define POLL_TIMEOUT_MS 1000


void exitError();
void exitErrorMsg(char* description);

void parseArgs(int argc, char* argv[], double* left, double* right, int* nChildren, double* maxDeviation, double* startFineness);

void createChildren(int parentPipes[][2], int nChildren);

void calcSums(double (*func)(double x), double left, double right, double* s, double* S, int nSegments);
void childCalcSums(int rd, int wr);

double parentIntegrate(int parentPipes[][2], int nChildren, double left, double right, double maxDeviation, double startFineness);

int main(int argc, char* argv[])
{
	double left, right, maxDeviation, startFineness;
	int nChildren;

	parseArgs(argc, argv, &left, &right, &nChildren, &maxDeviation, &startFineness);	

	int parentPipes[nChildren][2];
	createChildren(parentPipes, nChildren);

	double I = parentIntegrate(parentPipes, nChildren, left, right, maxDeviation, startFineness);

	kill(0, SIGCHLD);
	for (int i = 0; i < nChildren; i++)
		wait(NULL);

	printf("%lf\n", I);

	return 0;
}

void die(int sig)
{
	exit(EXIT_SUCCESS);
}

void childCalcSums(int rd, int wr)
{
	struct CalcRequest rq;
	struct ChildAnswer ans;

	signal(SIGCHLD, die);

	while (read(rd, &rq, sizeof(rq)))
	{
		calcSums(func, rq.left, rq.right, &(ans.lowSum), &(ans.highSum), rq.nSegments);

		write(wr, &ans, sizeof(ans));
	}
}

double parentIntegrate(int parentPipes[][2], int nChildren, double left, double right, double maxDeviation, double startFineness)
{
	double s;
	double S;

	struct CalcRequest rq;
	struct ChildAnswer ans;

	double partLen = (right - left) / nChildren;

	int nSegments = partLen / startFineness + 1;

	struct timeval start;
	gettimeofday(&start, NULL);

	struct pollfd fds[nChildren];
	for (int i = 0; i < nChildren; i++)
	{
		fds[i].fd = parentPipes[i][0];
		fds[i].events = POLLIN;

		rq.left = left + partLen * i; 
		rq.right = rq.left + partLen;
		rq.nSegments = nSegments;

		write(parentPipes[i][1], &rq, sizeof(rq));
	}


	do
	{
		S = s = 0;

		int done = 0;
		while (done < nChildren)
		{
			poll(fds, nChildren, POLL_TIMEOUT_MS);

			for (int i = 0; i < nChildren; i++)
			{
				if (fds[i].revents & POLLIN)
				{
					read(parentPipes[i][0], &ans, sizeof(ans));
					fds[i].events &= ~POLLIN;

					s += ans.lowSum;
					S += ans.highSum;
					done++;

					rq.left = left + partLen * i; 
					rq.right = rq.left + partLen;
					rq.nSegments = nSegments * 2;

					write(parentPipes[i][1], &rq, sizeof(rq));
				}
			}
		}

		for (int i = 0; i < nChildren; i++)
			fds[i].events |= POLLIN;

		nSegments *= 2;
	}
	while (S - s > maxDeviation);

	for (int i = 0; i < nChildren; i++)
	{
		close(parentPipes[i][0]);
		close(parentPipes[i][1]);
	}

	return (S + s) / 2;
}

void createChildren(int parentPipes[][2], int nChildren)
{
	int childPipes[2];
	int pipefd[2];

	for (int i = 0; i < nChildren; i++)
	{
		pipe(pipefd);
		parentPipes[i][1] = pipefd[1];
		childPipes[0] = pipefd[0];
		
		pipe(pipefd);
		parentPipes[i][0] = pipefd[0];
		childPipes[1] = pipefd[1];

		if (fork() == 0)
		{
			for (; i >= 0; i--) {
				close(parentPipes[i][0]);
				close(parentPipes[i][1]);
			}
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

void calcSums(double (*func)(double x), double left, double right, double* lowSum, double* highSum, int nSegments)
{
	double x = left, shift = (right - left) / nSegments; 
	double s = 0, S = 0;	
	double f1, f2 = func(x); 

	right -= shift;
	while (x <= right) 
	{
		f1 = f2;
		f2 = func(x += shift);
		
		if (f1 < f2) {
			s += f1 * shift;
			S += f2 * shift;
		} else {
			s += f2 * shift;
			S += f1 * shift;
		}
	}

	*lowSum = s;
	*highSum = S;
}

void parseArgs(int argc, char* argv[], double* left, double* right, int* nChildren, double* maxDeviation, double* startFineness)
{
	if (argc == 1)
		exitErrorMsg(
"Usage: ./integrate <from> <to> [nChildren] [maxDeviation] [startFineness]\n\n\
Calculates definite integral of function 'func', specified in 'libfunction.so'.\n\
'libfunction.so' is compiled from 'function.c'. To change the function, edit 'function.c', then run 'make'.\n\
All parameters except <nChildren> are of type double.\n"
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

	if (argc >= 6)
	{
		*startFineness = strtod(argv[5], &endptr);
		if (errno != 0 || (unsigned)(endptr - argv[5]) != strlen(argv[5]))
			exitErrorMsg("Failed to convert 5th argument to double.\n");
	}
		else *startFineness = 0.1;

	

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

