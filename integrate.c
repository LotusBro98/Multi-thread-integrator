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
	double lowSum;
	double highSum;

	struct timeval sent;
	struct timeval received;
	struct timeval sentBack;
};

struct UnstudiedSegment
{
	double left;
	double right;
	double lowSum;
	double highSum;
	struct UnstudiedSegment* next;
	struct UnstudiedSegment* prev;
};

struct timeval start;

#define true 1
#define false 0

#define POLL_TIMEOUT_MS 1000

void exitError();
void exitErrorMsg(char* description);

void parseArgs(int argc, char* argv[], double* left, double* right, int* nChildren, double* maxDeviation, double* startFineness);

void createChildren(int parentPipes[][2], int nChildren);

void calcSums(double (*func)(double x), double left, double right, double* s, double* S, int nSegments);
void childCalcSums(int rd, int wr);

double parentIntegrate(int parentPipes[][2], int nChildren, double left, double right, double maxDeviation, double startFineness);

void printTimes(struct ChildAnswer* answers, int nChildren);

void split(struct UnstudiedSegment* seg);
void splitNParts(struct UnstudiedSegment* seg, int n);
double removeSeg(struct UnstudiedSegment* *seg);
struct UnstudiedSegment* getSeg(struct UnstudiedSegment* head, int index);
struct UnstudiedSegment* initList(double left, double right, int nChildren);
void printList(struct UnstudiedSegment* list);
int listLen(struct UnstudiedSegment* list);


int main(int argc, char* argv[])
{
	double left, right, maxDeviation, startFineness;
	int nChildren;
		
	gettimeofday(&start, NULL);

	parseArgs(argc, argv, &left, &right, &nChildren, &maxDeviation, &startFineness);	

	int parentPipes[nChildren][2];
	createChildren(parentPipes, nChildren);

	double I = parentIntegrate(parentPipes, nChildren, left, right, maxDeviation, startFineness);

	for (int i = 0; i < nChildren; i++)
		wait(NULL);

	struct rusage res;
	getrusage(RUSAGE_CHILDREN, &res);
	printf("%ld\n", res.ru_nvcsw);

	printf("%lf\n", I);

	return 0;
}

void childCalcSums(int rd, int wr)
{
	struct CalcRequest rq;
	struct ChildAnswer ans;

	while (read(rd, &rq, sizeof(rq)))
	{
		ans.sent = rq.sent;
		gettimeofday(&ans.received, NULL);
		calcSums(func, rq.left, rq.right, &(ans.lowSum), &(ans.highSum), rq.nSegments);
		gettimeofday(&ans.sentBack, NULL);

		write(wr, &ans, sizeof(ans));
	}
}

void makeRequest(struct UnstudiedSegment* seg, struct CalcRequest* rq)
{
	rq->left = seg->left;
	rq->right = seg->right;
	rq->nSegments = 128;
	gettimeofday(&(rq->sent), NULL);
}

void fillSegmentData(struct UnstudiedSegment* seg, struct ChildAnswer* ans)
{
	seg->lowSum = ans->lowSum;
	seg->highSum = ans->highSum;
}

void collectData(struct pollfd* fds, int nChildren, struct UnstudiedSegment* segList)
{
	struct ChildAnswer ans;

	int done = 0;
	while (done < nChildren)
	{
		poll(fds, nChildren, POLL_TIMEOUT_MS);

		for (int i = 0; i < nChildren; i++)
			if (fds[i].revents & POLLIN)
			{
				read(fds[i].fd, &ans, sizeof(ans));
				fillSegmentData(getSeg(segList, i), &ans);
				
				fds[i].events &= ~POLLIN;
				done++;
			}
	}
}

double pickGoodIntegrals(struct UnstudiedSegment** segList, int nChildren, double maxDeviation, double left, double right)
{
	double I = 0;
	double dens = maxDeviation / (right - left);

	struct UnstudiedSegment* p = *segList;

	printList(*segList);
	for (int i = 0; i < nChildren; i++)
	{
		printf("%le %le\n", p->highSum - p->lowSum, dens * (p->right - p->left));
		if ((p->highSum - p->lowSum) <= dens * (p->right - p->left))
		{
			if (p == *segList)
			{
				I += removeSeg(&p);
				*segList = p;
			}
			else
				I += removeSeg(&p);
		}
		else
		{
			split(p);
			p = (p->next)->next;
		}
	}

	if (*segList != NULL && listLen(*segList) < nChildren)
		splitNParts(*segList, nChildren - listLen(*segList) + 1);

	return I;
}

void sendRequests(struct pollfd* fds, int pipes[][2], int nChildren, struct UnstudiedSegment* segList)
{
	struct CalcRequest rq;
	struct UnstudiedSegment* p = segList;

	for (int i = 0; i < nChildren; i++)
	{
		fds[i].fd = pipes[i][0];
		fds[i].events = POLLIN;
		makeRequest(p, &rq);
		write(pipes[i][1], &rq, sizeof(rq));
		p = p->next;
	}
}

double parentIntegrate(int parentPipes[][2], int nChildren, double left, double right, double maxDeviation, double startFineness)
{
	double I = 0;

	struct pollfd fds[nChildren];

	struct UnstudiedSegment* segList = initList(left, right, nChildren);

	do
	{
		sendRequests(fds, parentPipes, nChildren, segList);
		collectData(fds, nChildren, segList);
		I += pickGoodIntegrals(&segList, nChildren, maxDeviation, left, right);
	}
	while (segList != NULL);

	for (int i = 0; i < nChildren; i++)
	{
		close(parentPipes[i][0]);
		close(parentPipes[i][1]);
	}

	return I;
}

void calcSums(double (*func)(double x), double left, double right, double* lowSum, double* highSum, int nSegments)
{
	double shift = (right - left) / nSegments; 
	double fleft = func(left), fright = func(right);
	double f1, f2 = func(left); 
	double s = 0, S = 0;	
	double x;

	for (int i = 1; i <= nSegments; i++)
	{
		f1 = f2;
		x = left + i * (right - left) / nSegments;
		f2 = func(x) - fleft - i * (fright - fleft) / nSegments; 
		
		if (f1 < f2) {
			s += f1 * shift;
			S += f2 * shift;
		} else {
			s += f2 * shift;
			S += f1 * shift;
		}
	}

	s += (fright + fleft) / 2 * (right - left);
	S += (fright + fleft) / 2 * (right - left);

	*lowSum = s;
	*highSum = S;
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

	for (int i = 2; i <= n; i++)	
	{
		struct UnstudiedSegment* newSeg = malloc(sizeof(struct UnstudiedSegment));
		double div = left + i * (right - left) / n;
		newSeg->left = p->right;
		newSeg->right = div;

		if (p->next != NULL)
			p->next->prev = newSeg;
		newSeg->prev = p;

		newSeg->next = p->next;
		p->next = newSeg;
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
	
	double I = ((*seg)->highSum + (*seg)->lowSum) / 2;

	if ((*seg)->next != NULL)
		(*seg)->next->prev = (*seg)->prev;

	if ((*seg)->prev != NULL)
		(*seg)->prev->next = (*seg)->next;

	struct UnstudiedSegment* old = *seg;
	*seg = old->next;
	free(old);

	return I;
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

struct UnstudiedSegment* getSeg(struct UnstudiedSegment* head, int index)
{
	if (head == NULL)
		return NULL;

	struct UnstudiedSegment* p = head;
	for (int i = 0; i < index; i++)
	{
		p = p->next;
		if (p == NULL)
			return NULL;
	}

	return p;
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
		printf("[%2.2le (%2.2le %2.2le) %2.2le]", list->left, list->lowSum, list->highSum, list->right);
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

void printTimes(struct ChildAnswer* answers, int nChildren)
{
	long minTime = getMillis(answers[0].sent);
	long maxTime = getMillis(answers[0].sentBack);

	for (int i = 0; i < nChildren; i++)
	{
		if (getMillis(answers[i].sent) < minTime)
			minTime = getMillis(answers[i].sent);

		if (getMillis(answers[i].sentBack) > maxTime)
			maxTime = getMillis(answers[i].sentBack);
	}

	int dots = 100;
	double millisPerDot = (maxTime - minTime) / (double)dots;
	double startTime = minTime;

	printf("%4ld", minTime - getMillis(start));
	for (int i = 8; i < dots; i++)
		printf("-");
	printf("%4ld\n", maxTime - getMillis(start));

	for (int i = 0; i < nChildren; i++)
	{
		double t = startTime;
		for (; t < getMillis(answers[i].sent); t += millisPerDot)
			printf(" ");

		printf("\033[91m");
		for (; t < getMillis(answers[i].received); t += millisPerDot)
			printf("*");

		printf("\033[92m");
		for (; t < getMillis(answers[i].sentBack); t += millisPerDot)
			printf("*");

		printf("\033[0m\n");
	}
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

