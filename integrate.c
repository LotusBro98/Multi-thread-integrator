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
#include <poll.h>
#include <wait.h>
#include <sched.h>
#include <math.h>

extern double func(double x);

struct CalcRequest
{
	double left;
	double right;
	int nSegments;

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

struct UnstudiedSegment
{
	double left;
	double right;
	double S;

	struct UnstudiedSegment* next;
	struct UnstudiedSegment* prev;

	int child;
};

struct timeval start;

#define true 1
#define false 0

#define POLL_TIMEOUT_MS 1000
#define SEGMENTS_PER_PROCESS 0x1000
#define PRINT_PERIOD_MS 100

void exitError();
void exitErrorMsg(char* description);

void parseArgs(int argc, char* argv[], double* left, double* right, int* nChildren, double* maxDeviation);

void createChildren(int parentPipes[][2], int nChildren);

void calcSums(double (*func)(double x), double left, double right, double* s, double* S, int nSegments);
void childCalcSums(int rd, int wr);

double parentIntegrate(int parentPipes[][2], int nChildren, double left, double right, double maxDeviation);

void printTimes(struct ChildAnswer* answers, int nChildren);
void printProgress(struct UnstudiedSegment* segList, double left, double right, double I);

void split(struct UnstudiedSegment* seg);
void splitNParts(struct UnstudiedSegment* seg, int n);
double removeSeg(struct UnstudiedSegment* *seg);
struct UnstudiedSegment* getSeg(struct UnstudiedSegment* head, int index);
struct UnstudiedSegment* initList(double left, double right, int nChildren);
void printList(struct UnstudiedSegment* list);
int listLen(struct UnstudiedSegment* list);

void printAnswer(double left, double right, double maxDeviation, double I)
{
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

int main(int argc, char* argv[])
{
	double left, right, maxDeviation;
	int nChildren;
		
	gettimeofday(&start, NULL);

	parseArgs(argc, argv, &left, &right, &nChildren, &maxDeviation);	

	int parentPipes[nChildren][2];
	createChildren(parentPipes, nChildren);

	double I = parentIntegrate(parentPipes, nChildren, left, right, maxDeviation);

	for (int i = 0; i < nChildren; i++)
		wait(NULL);

//	printf("%.20f\n", I);
	printAnswer(left, right, maxDeviation, I);

	return 0;
}

void makeRequest(struct UnstudiedSegment* seg, struct CalcRequest* rq, int child)
{
	rq->left = seg->left;
	rq->right = seg->right;
	rq->nSegments = SEGMENTS_PER_PROCESS;

	gettimeofday(&(rq->sent), NULL);

	seg->child = child;
}

int handleSegmentData(
	struct UnstudiedSegment** segList, 
	struct ChildAnswer* ans, 
	struct CalcRequest* rq,
	int child,
	double* I, 
	double dens)
{
	struct UnstudiedSegment* seg = getSeg(*segList, child);

	if (ans->eps < dens)
	{
		seg->S = ans->S;
		if (seg == *segList)
			*I += removeSeg(segList);
		else
			*I += removeSeg(&seg);

		seg = getSeg(*segList, -1);
		if (seg == NULL)
			return false;

		makeRequest(seg, rq, child);
		return true;
	}
	else
	{
		split(seg);
		makeRequest(seg, rq, child);
		return true;
	}
}

void sendRequests(struct pollfd* fds, int pipes[][2], struct ChildAnswer* answers, int nChildren, struct UnstudiedSegment* segList)
{
	struct CalcRequest rq;
	struct UnstudiedSegment* seg;;

	int nSeg = listLen(segList);
	if (nSeg < nChildren)
		splitNParts(segList, nChildren - nSeg + 1);

	for (int i = 0; i < nChildren; i++)
	{
		if (getSeg(segList, i) == NULL)
		{
			seg = getSeg(segList, -1);
			if (seg == NULL)
				break;

			makeRequest(seg, &rq, i);
			write(pipes[i][1], &rq, sizeof(rq));
		}

		answers[i].sent = start;
		answers[i].received = start;
		answers[i].sentBack = start;
		
		fds[i].fd = pipes[i][0];
		fds[i].events = POLLIN;
	}
}

double parentIntegrate(int pipes[][2], int nChildren, double left, double right, double maxDeviation)
{
	double dens = maxDeviation / (right - left);
	struct ChildAnswer answers[nChildren];
	struct pollfd fds[nChildren];
	struct CalcRequest rq;
	struct UnstudiedSegment* segList = initList(left, right, nChildren);
	double I = 0;

	sendRequests(fds, pipes, answers, nChildren, segList);

	while (segList != NULL)
	{
		poll(fds, nChildren, POLL_TIMEOUT_MS);

		for (int i = 0; i < nChildren; i++)
			if (fds[i].revents & POLLIN)
			{
				read(fds[i].fd, &(answers[i]), sizeof(answers[i]));
				

				if(handleSegmentData(&segList, &(answers[i]), &rq, i, &I, dens))
					write(pipes[i][1], &rq, sizeof(rq));
			}

		printProgress(segList, left, right, I);
	}

	for (int i = 0; i < nChildren; i++)
	{
		close(pipes[i][0]);
		close(pipes[i][1]);
	}

	return I;
}

void childCalcSums(int rd, int wr)
{
	struct CalcRequest rq;
	struct ChildAnswer ans;

	ans.sent = start;
	while (read(rd, &rq, sizeof(rq)))
	{
		gettimeofday(&ans.received, NULL);
		calcSums(func, rq.left, rq.right, &(ans.S), &(ans.eps), rq.nSegments);
		gettimeofday(&ans.sentBack, NULL);

		write(wr, &ans, sizeof(ans));
		gettimeofday(&ans.sent, NULL);
	}
}


void calcSums(double (*func)(double x), double left, double right, double* I, double* eps, int nSegments)
{
	double fleft = func(left), fright = func(right);
	double f1, f2 = 0; 
	double s = 0, S = 0;	
	double x;

	for (int i = 1; i <= nSegments; i++)
	{
		f1 = f2;
		x = left + i * (right - left) / nSegments;
		f2 = func(x) - fleft - i * (fright - fleft) / nSegments; 

		if (f1 < f2) {
			s += f1;
			S += f2;
		} else {
			s += f2;
			S += f1;
		}
	}

	*eps = (S - s) / nSegments;
	*I = (S + s) / 2.0 / nSegments + (fright + fleft) / 2;
}

void die(int sig)
{
	if (sig == SIGCHLD)
		exit(EXIT_SUCCESS);
}

void createChildren(int parentPipes[][2], int nChildren)
{
	int childPipes[2];
	int pipefd[2];

	signal(SIGCHLD, die);
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
	signal(SIGCHLD, SIG_DFL);
}

struct UnstudiedSegment* initList(double left, double right, int nChildren)
{
	struct UnstudiedSegment* list = malloc(sizeof(struct UnstudiedSegment));

	list->left = left;
	list->right = right;
	list->next = NULL;
	list->prev = NULL;

	splitNParts(list, nChildren);

	return list;
}

void splitNParts(struct UnstudiedSegment* seg, int n)
{
	if (seg == NULL)
		return;

	double left = seg->left;
	double right = seg->right;

	struct UnstudiedSegment* p = seg;
	p->right = left + (right - left) / n;
	p->child = -1;

	for (int i = 2; i <= n; i++)	
	{
		struct UnstudiedSegment* newSeg = malloc(sizeof(struct UnstudiedSegment));
		double div = left + i * (right - left) / n;
		newSeg->left = p->right;
		newSeg->right = div;
		newSeg->child = -1;

		if (p->next != NULL)
			p->next->prev = newSeg;
		newSeg->prev = p;

		newSeg->next = p->next;
		p->next = newSeg;

		p = p->next;
	}
}

void split(struct UnstudiedSegment* seg)
{
	if (seg == NULL)
		return;

	struct UnstudiedSegment* newSeg = malloc(sizeof(struct UnstudiedSegment));
	if (errno != 0)
	{
		fprintf(stderr, "Failed to allocate memory for another segment in segList.\n");
		exit(EXIT_FAILURE);
	}

	double center = (seg->right + seg->left) / 2;

	newSeg->left = center;
	newSeg->right = seg->right;
	seg->right = center;

	seg->child = -1;
	newSeg->child = -1;

	if (seg->next != NULL)
		seg->next->prev = newSeg;
	newSeg->prev = seg;

	newSeg->next = seg->next;
	seg->next = newSeg;
}

double removeSeg(struct UnstudiedSegment* *seg)
{
	if (*seg == NULL)
		return 0;
	
	double DI = (*seg)->S * ((*seg)->right - (*seg)->left);

	if ((*seg)->next != NULL)
		(*seg)->next->prev = (*seg)->prev;

	if ((*seg)->prev != NULL)
		(*seg)->prev->next = (*seg)->next;

	struct UnstudiedSegment* old = *seg;
	*seg = old->next;
	free(old);

	return DI;
}

int listLen(struct UnstudiedSegment* list)
{
	int i = 0;
	while (list)
	{
		list = list->next;
		i++;
	}
	return i;
}

struct UnstudiedSegment* getSeg(struct UnstudiedSegment* head, int child)
{
	if (head == NULL)
		return NULL;

	for (struct UnstudiedSegment* p = head; p != NULL; p = p->next)
	{
		if (p->child == child)
			return p;
	}
	return NULL;
}

void printList(struct UnstudiedSegment* list)
{
	if (list == NULL)
	{
		printf("NULL\n");
		return;
	}

	while (list)
	{
		printf("[%2.2lg :%d: %2.2lg]", list->left, list->child, list->right);
		if (list->next)
			printf(" -> ");
		list = list->next;
	}

	printf("\n");
}

long getMillis(struct timeval tv)
{
	return (tv.tv_sec % 10000) * 1000 + tv.tv_usec / 1000;
}

long getMicros(struct timeval tv)
{
	return (tv.tv_sec % 100) * 1000000 + tv.tv_usec;
}

struct timeval lastPrint;
int firstTime = true;
void printProgress(struct UnstudiedSegment* segList, double left, double right, double I)
{
	int dots = 100;
	double unitsPerDot = (right - left) / dots;
	double x = left;
	struct UnstudiedSegment* p = segList;

	struct timeval tv;
	gettimeofday(&tv, NULL);

	if (p != NULL && getMillis(tv) - getMillis(lastPrint) < PRINT_PERIOD_MS)
		return;
	else
		lastPrint = tv;

	if (firstTime)
		firstTime = false;
	else
	{
		fprintf(stderr, "\033M");
		fflush(stdout);
	}

	while (x < right)
	{
		if (p != NULL && x > p->left)
			fprintf(stderr, "\033[91m-\033[0m");
		else
			fprintf(stderr, "\033[93m+\033[0m");

		if (p != NULL && p->next != NULL && x > p->next->left)
			p = p->next;

		x += unitsPerDot;
	}

	fprintf(stderr, "  %.18lf\n", I);
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

	printf("%4ld", minTime - getMicros(start));
	for (int i = 8; i < dots; i++)
		printf("-");
	printf("%4ld\n", maxTime - getMicros(start));

	printf("\n");

	for (int i = 0; i < nChildren; i++)
	{
		double t = startTime;
		for (; t < getMicros(answers[i].sent); t += microsPerDot)
			printf(" ");

		printf("\033[91m");
		for (; t < getMicros(answers[i].received); t += microsPerDot)
			printf("*");

		printf("\033[92m");
		for (; t < getMicros(answers[i].sentBack); t += microsPerDot)
			printf("*");

		for (t -= microsPerDot; t < maxTime; t += microsPerDot)
			printf(" ");

		printf("\033[0m\n");
	}
}

void parseArgs(int argc, char* argv[], double* left, double* right, int* nChildren, double* maxDeviation)
{
	if (argc == 1)
		exitErrorMsg(
"Usage: ./integrate <from> <to> [nChildren] [maxDeviation]\n\n\
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

