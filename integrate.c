#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

extern double func(double x);

void exitError();
void exitErrorMsg(char* description);

double integrate(double (*func)(double x), double left, double right, double maxDeviation);

void calcSums(double (*func)(double x), double left, double right, double* s, double* S, int nSegments);


int main(int argc, char* argv[])
{
	if (argc == 1)
		exitErrorMsg(
"Usage: ./integrate <from> <to> <maxDeviation>\n\n\
Calculates definite integral of function 'func', specified in 'libfunction.so'.\n\
'libfunction.so' is compiled from 'function.c'. To change the function, edit 'function.c', then run 'make'.\n\
All parameters are of type double.\n"
		);
	else if (argc != 4)
		exitErrorMsg("Wrong format. Type './integrate' for help.\n");

	char* endptr;
	double left  = strtod(argv[1], &endptr);
	if (errno != 0 || (unsigned)(endptr - argv[1]) != strlen(argv[1]))
		exitErrorMsg("Failed to convert 1st argument to double.\n");

	double right = strtod(argv[2], &endptr);
	if (errno != 0 || (unsigned)(endptr - argv[2]) != strlen(argv[2]))
		exitErrorMsg("Failed to convert 2nd argument to double.\n");

	double maxDeviation = strtod(argv[3], &endptr);
	if (errno != 0 || (unsigned)(endptr - argv[3]) != strlen(argv[3]))
		exitErrorMsg("Failed to convert 3rd argument to double.\n");

	printf("%lf\n", integrate(func, left, right, maxDeviation));
	
	return 0;
}


void calcSums(double (*func)(double x), double left, double right, double* lowSum, double* highSum, int nSegments)
{
	double shift = (right - left) / nSegments;	
	double x = left, f1, f2 = func(left), m, M, s = 0, S = 0; 

	right -= shift;
	while (x <= right) 
	{
		f1 = f2;
		f2 = func(x += shift);
		
		if (f1 < f2) {
			m = f1;
			M = f2;
		} else {
			m = f2;
			M = f1;
		}

		s += m * shift;
		S += M * shift;
	}

	*lowSum = s;
	*highSum = S;
}


double integrate(double (*func)(double x), double left, double right, double maxDeviation)
{
	double s;
	double S;
	int nSegments = 32;

	do {
		calcSums(func, left, right, &s, &S, nSegments);
		nSegments *= 2;
	} while (S - s > maxDeviation);

	return (S + s) / 2;
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

