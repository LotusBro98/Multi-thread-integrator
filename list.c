#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "list.h"

struct SegmentList initList(double left, double right)
{
	struct UnstudiedSegment* head = malloc(sizeof(struct UnstudiedSegment));
	struct UnstudiedSegment* first = malloc(sizeof(struct UnstudiedSegment));

	first->left = left;
	first->right = right;
	first->next = head;
	first->prev = head;
	first->child = -1;

	head->next = first;
	head->prev = first;

	struct SegmentList list = {.head = head};
	list.startLen = right - left;

	return list;
}

void printList(struct SegmentList list)
{
	if (isEmpty(list))
	{
		printf("EMPTY\n");
		return;
	}

	struct UnstudiedSegment* p = list.head->next;

	for (; p != list.head; p = p->next)
	{
		if (p->child != -1)
			printf("[%d %.1lf] ", p->child, p->right - p->left);
		else
			printf("%.1lf ", p->right - p->left);

		if (p->next != list.head)
			printf(" -> ");
	}

	printf("\n");
}

int isEmpty(struct SegmentList list)
{
	return list.head->next == list.head;
}

void destroyList(struct SegmentList list)
{
	while (!isEmpty(list))
		removeSeg(list.head->next);

	free(list.head);
}

struct UnstudiedSegment* findPlace(struct SegmentList list, double len)
{
	struct UnstudiedSegment* p = list.head->next;

	while (p != list.head && (p->right - p->left) > len && p->child == -1)
		p = p->next;

	return p;
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

	seg->next->prev = newSeg;
	newSeg->prev = seg;

	newSeg->next = seg->next;
	seg->next = newSeg;
}

/*
void redoubleViligance(struct UnstudiedSegment* seg)
{
	if (seg->nSegments < MAX_SEGMENTS_PER_PROCESS)
	{
		seg->child = -1;
		seg->nSegments *= 2;
		if (seg->nSegments > MAX_SEGMENTS_PER_PROCESS)
			seg->nSegments = MAX_SEGMENTS_PER_PROCESS;
	}
	else
	{
		seg->nSegments /= 2;
		if (seg->nSegments < START_LITTLE_SEGMENTS)
			seg->nSegments = START_LITTLE_SEGMENTS;

		split(seg);
	}
}
*/

double removeSeg(struct UnstudiedSegment* seg)
{
	if (seg == NULL || seg->next == seg)
		return 0;
	
	//double DI = seg->S * (seg->right - seg->left);
	double DI = seg->S;

	seg->next->prev = seg->prev;
	seg->prev->next = seg->next;

	free(seg);

	return DI;
}

int listLen(struct SegmentList segList)
{
	struct UnstudiedSegment* p = segList.head->next;

	int i = 0;
	while (p != segList.head)
	{
		p = p->next;
		i++;
	}
	return i;
}

struct UnstudiedSegment* getSeg(struct SegmentList list, int child)
{
	if (isEmpty(list))
		return NULL;

	for (struct UnstudiedSegment* p = list.head->next; p != list.head; p = p->next)
	{
		if (p->child == child)
			return p;
	}
	return NULL;
}

struct UnstudiedSegment* getWidestFree(struct SegmentList list)
{
	struct UnstudiedSegment* max = NULL;
	double maxLen = -1;

	for (struct UnstudiedSegment* p = list.head->next; p != list.head; p = p->next)
	{
		if (((p->right - p->left) > maxLen) && (p->child == -1))
		{
			max = p;
			maxLen = p->right - p->left;
		}
	}

	return max;
}

