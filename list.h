#ifndef LIST_H
#define LIST_H

struct UnstudiedSegment
{
	double left;
	double right;
	double S;

	struct UnstudiedSegment* next;
	struct UnstudiedSegment* prev;

	int child;
};

struct SegmentList
{
	struct UnstudiedSegment* head;
	double startLen;
};

void split(struct UnstudiedSegment* seg);
void splitNParts(struct UnstudiedSegment* seg, int n);
double removeSeg(struct UnstudiedSegment* seg);
struct UnstudiedSegment* getSeg(struct SegmentList segList, int child);
struct SegmentList initList(double left, double right, int nChildren);
void printList(struct SegmentList list);
int listLen(struct SegmentList list);
int isEmpty(struct SegmentList list);
void destroyList(struct SegmentList list);
struct UnstudiedSegment* getWidestFree(struct SegmentList list);

#endif
