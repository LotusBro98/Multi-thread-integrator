#ifndef UI_H
#define UI_H

#define PRINT_PERIOD_MS 100

#include "list.h"
#include "general.h"

void exitError();
void exitErrorMsg(char* description);

void initTiming(struct ChildAnswer* answers, int nChildren);
void printTimes(struct ChildAnswer* answers, int nChildren);
void printProgress(struct SegmentList segList, double left, double right, double I);
//void printProgress(struct SegmentList segList, double left, double right, double I, struct ChildAnswer* answers, int nChildren);
void printAnswer(double left, double right, double maxDeviation, double I);

void parseArgs(int argc, char* argv[], double* left, double* right, int* nChildren, double* maxDeviation);

long getMicros(struct timeval tv);

#endif
