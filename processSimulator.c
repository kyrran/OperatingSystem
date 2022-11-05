#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include "coursework.h"
#include <string.h>
#include <semaphore.h>
#include <time.h>
#include "linkedlist.h"

/*----------------------------------------------DECLARE FUNCTIONS HERE----------------------------------------------------------*/
void printHeadersSVG();
void printProcessSVG(int iCPUId, struct process *pProcess, struct timeval oStartTime, struct timeval oEndTime);
void printPrioritiesSVG();
void printRasterSVG();
void printFootersSVG();
void *f_shortScheduler(void *cpuID);
void *f_booster(void * arg);
void *f_termination(void * arg);
void *f_longScheduler(void * arg);
void *f_processGenerator(void * arg);
/*---------------------------------------------DECLARE GLOBAL VARIABLES HERE------------------------------------------------------*/
sem_t ready_sem_sync;
sem_t sync_sem;
sem_t sts_sem_full;

pthread_mutex_t mutex_pcb;
pthread_mutex_t terQ;
pthread_mutex_t mutex_newQ;

struct process *arr_processTable[SIZE_OF_PROCESS_TABLE];
struct element *arr_ready_head[MAX_PRIORITY];
struct element *arr_ready_tail[MAX_PRIORITY];
int arr_CPUIDs[NUMBER_OF_CPUS];
int arr_pids[SIZE_OF_PROCESS_TABLE];

pthread_t arr_threadIDs[NUMBER_OF_CPUS + 4];

struct element *ter_head = NULL;
struct element *ter_tail = NULL;
struct element *newQ_head = NULL;
struct element *newQ_tail = NULL;
struct element *free_pid_head = NULL;
struct element *free_pid_tail = NULL;

struct timeval oBaseTime;
int processRemoved = 0;
long int cw_ResponseTime = 0.0;
long int cw_turnAroundTime = 0.0;
/*-----------------------------------------GLOBAL VARIABLE ENDS-----------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
	printHeadersSVG();
	printPrioritiesSVG();
	printRasterSVG();
	printf("\n\n");

	int i=0;
	pthread_mutex_init(&mutex_pcb, NULL);
	pthread_mutex_init(&mutex_newQ, NULL);
	pthread_mutex_init(&terQ, NULL);

	sem_init(&ready_sem_sync, 0, 1);
	sem_init(&sync_sem, 0, 1);
	sem_init(&sts_sem_full, 0, 0);

	gettimeofday(&oBaseTime, NULL);
	/*Initialise pid array*/
	for (i = 0; i < SIZE_OF_PROCESS_TABLE; i++)
	{
		arr_pids[i] = i;
	}
	/*initialise pid free list*/
	for (i = 0; i < SIZE_OF_PROCESS_TABLE; i++)
	{
		if (arr_processTable[arr_pids[i]] == NULL)
		{
			addLast((void *)&arr_pids[i], &free_pid_head, &free_pid_tail);
		}
	}
	/*initialise 0-31 ready queues with priority*/
	for (i = 0; i < MAX_PRIORITY; i++)
	{
		arr_ready_head[i] = NULL;
		arr_ready_tail[i] = NULL;
	}
	//two threads to run on cpu
	for (i = 0; i < NUMBER_OF_CPUS; i++)
	{
		arr_CPUIDs[i] = i + 1;
		pthread_create(&arr_threadIDs[i], NULL, &f_shortScheduler, (void *)&arr_CPUIDs[i]);
	}

	pthread_create(&arr_threadIDs[NUMBER_OF_CPUS], NULL, &f_processGenerator, NULL);
	pthread_create(&arr_threadIDs[NUMBER_OF_CPUS + 1], NULL, &f_longScheduler, NULL);
	pthread_create(&arr_threadIDs[NUMBER_OF_CPUS + 2], NULL, &f_termination, NULL);
	pthread_create(&arr_threadIDs[NUMBER_OF_CPUS + 3], NULL, &f_booster, NULL);

	for (int i = 0; i < NUMBER_OF_CPUS + 4; i++)
	{
		pthread_join(arr_threadIDs[i], NULL);
		
	}
	pthread_mutex_destroy(&mutex_pcb);
	pthread_mutex_destroy(&mutex_newQ);
	pthread_mutex_destroy(&terQ);
	
	sem_close(&ready_sem_sync);
	sem_close(&sts_sem_full);
	sem_close(&sync_sem);

	free(free_pid_head);
	free(free_pid_tail);
	free(newQ_head);
	free(newQ_tail);
	free(ter_head);
	free(ter_tail);
}

/*----------------------------------------------------PROCESS GENERATOR FUNCTION------------------------------------------------------------------*/
/**
 * find free pid
 * generate new process with this pid
 * add new process to new queue
*/
void *f_processGenerator(void * arg)
{
	sem_wait(&sync_sem);
	int processCreated =0;

	while (processCreated < NUMBER_OF_PROCESSES)
	{
		pthread_mutex_lock(&mutex_pcb);
		/*remove the free pid*/
		int *pid_first = removeFirst(&free_pid_head, &free_pid_tail);
		struct element *free_temp = free_pid_head;
		/*generate the process with that pid an put it into corresponding index of process table*/
		arr_processTable[*pid_first] = generateProcess(pid_first);
		/*track how many process you have been created*/
		processCreated++;
		pthread_mutex_unlock(&mutex_pcb);

		
		pthread_mutex_lock(&mutex_newQ);
		/*add this process to new Queue*/
		addLast(arr_processTable[*pid_first], &newQ_head, &newQ_tail);
		/*get the newly added process data to print out message*/
		struct process *newP = newQ_tail->pData;
		printf("TXT: Generated: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *(int *)newP->pPID, newP->iPriority, newP->iPreviousBurstTime, newP->iRemainingBurstTime);
		pthread_mutex_unlock(&mutex_newQ);

		if (free_temp == NULL)
		{
			sem_wait(&sync_sem);
		}
	}

}


/*----------------------------------------------------------------LONG TERM SCHEDULER-------------------------------------------------------------------*/
/**
 * runs per 50ms
 * remove process from newqueue
 * add process to readyqueue
*/
void *f_longScheduler(void * arg)
{
	while (1)
	{
		if (newQ_head)
		{
			pthread_mutex_lock(&mutex_newQ);
			/*remove the first process in newQueue*/
			struct process *p = removeFirst(&newQ_head, &newQ_tail);
			pthread_mutex_unlock(&mutex_newQ);
			
			sem_wait(&ready_sem_sync);
			/*get it priority*/
			int pPri = p->iPriority;
			/*add it to corresponding ready queue under that prioriry*/
			addLast(p, &arr_ready_head[pPri], &arr_ready_tail[pPri]);
			printf("TXT: Admitted: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *(int *)p->pPID, pPri, p->iPreviousBurstTime, p->iRemainingBurstTime);
			sem_post(&ready_sem_sync);
			sem_post(&sts_sem_full);
		}
		/*per 50ms*/
		usleep(LONG_TERM_SCHEDULER_INTERVAL);
	}
}


/*------------------------------------------------------------------SHORT TIME SCHEDULER-------------------------------------------------------*/
/**
 * remove process from ready queue
 * simulate them
 * add them back to end of ready queue or to terminated queue
*/
void *f_shortScheduler(void *cpuID)
{
	while (1)
	{
		int arr_index=0;
		struct timeval oStartTime;
		struct timeval oEndTime;

		sem_wait(&sts_sem_full);
		int x = 0;

		sem_wait(&ready_sem_sync);
		for (x = 0; x < MAX_PRIORITY; x++)
		{
			if (arr_ready_head[x]!=NULL)
			{
				break;
			}
		}

		/*remove the process from ready queue*/
		struct process *processRemoved = removeFirst(&arr_ready_head[x], &arr_ready_tail[x]);
		sem_post(&ready_sem_sync);
		
		/*check wether first time to run*/
		int checkFirst = 0;
		if (processRemoved->iInitialBurstTime == processRemoved->iRemainingBurstTime)
		{
			checkFirst = 1;
		}

		/*16-31 run RR*/
		if (x >= MAX_PRIORITY / 2)
		{
			runPreemptiveJob(processRemoved, &oStartTime, &oEndTime);
		}
		else if (x < MAX_PRIORITY / 2)
		{
			/*0-15 run FCFS*/
			for (arr_index = 0; arr_index < x; arr_index++)
			{
				if (arr_ready_head[arr_index])
				{
					preemptJob(processRemoved);
					break;
				}
			}
			runNonPreemptiveJob(processRemoved, &oStartTime, &oEndTime);
		}

		/*response time*/
		long rt = getDifferenceInMilliSeconds(processRemoved->oTimeCreated, processRemoved->oFirstTimeRunning);
		if (checkFirst == 1)
		{
			cw_ResponseTime += rt;
		}

		/*if this process has finished*/
		if (processRemoved->iRemainingBurstTime == 0)
		{
			/*turn around time*/
			long tr_Time = getDifferenceInMilliSeconds(processRemoved->oTimeCreated, processRemoved->oLastTimeRunning);
			cw_turnAroundTime += tr_Time;

			pthread_mutex_lock(&terQ);
			/*add this process to end of termination queue*/
			addLast(processRemoved, &ter_head, &ter_tail);
			pthread_mutex_unlock(&terQ);
			
			/*if priority is 16-31*/
			if (processRemoved->iPriority >= MAX_PRIORITY / 2)
			{
				if (checkFirst == 1)
				{
					printf("TXT: Consumer %d, Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %ld, Turnaround Time = %ld\n", *(int *)cpuID, *(int *)processRemoved->pPID, processRemoved->iPriority, processRemoved->iPreviousBurstTime, processRemoved->iRemainingBurstTime, rt, tr_Time);
					printProcessSVG(*(int *)cpuID, processRemoved, oStartTime, oEndTime);
				}
				else if (checkFirst == 0)
				{
					printf("TXT: Consumer %d, Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Turnaround Time = %ld\n", *(int *)cpuID, *(int *)processRemoved->pPID, processRemoved->iPriority, processRemoved->iPreviousBurstTime, processRemoved->iRemainingBurstTime, tr_Time);
					printProcessSVG(*(int *)cpuID, processRemoved, oStartTime, oEndTime);
				}
			}
			/*if priority is 0-15*/
			else if (processRemoved->iPriority < MAX_PRIORITY / 2)
			{
				if (checkFirst == 1)
				{
					printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %ld, Turnaround Time = %ld\n", *(int *)cpuID, *(int *)processRemoved->pPID, processRemoved->iPriority, processRemoved->iPreviousBurstTime, processRemoved->iRemainingBurstTime, rt, tr_Time);
					printProcessSVG(*(int *)cpuID, processRemoved, oStartTime, oEndTime);
				}
				else if (checkFirst == 0)
				{
					printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Turnaround Time = %ld\n", *(int *)cpuID, *(int *)processRemoved->pPID, processRemoved->iPriority, processRemoved->iPreviousBurstTime, processRemoved->iRemainingBurstTime, tr_Time);
					printProcessSVG(*(int *)cpuID, processRemoved, oStartTime, oEndTime);
				}
			}
			printf("TXT: Terminated: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *(int *)processRemoved->pPID, processRemoved->iPriority, processRemoved->iPreviousBurstTime, processRemoved->iRemainingBurstTime);
		}
		/*if the process hasnt finished itself*/
		else
		{
			sem_wait(&ready_sem_sync);
			/*add it back to end f=of ready queue*/
			addLast(processRemoved, &arr_ready_head[processRemoved->iPriority], &arr_ready_tail[processRemoved->iPriority]);
			sem_post(&sts_sem_full);
			sem_post(&ready_sem_sync);
			/*if priorify if 16-31*/
			if (processRemoved->iPriority >= MAX_PRIORITY / 2)
			{
				if (checkFirst == 1)
				{
					printf("TXT: Consumer %d, Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %ld\n", *(int *)cpuID, *(int *)processRemoved->pPID, processRemoved->iPriority, processRemoved->iPreviousBurstTime, processRemoved->iRemainingBurstTime, rt);
					printProcessSVG(*(int *)cpuID, processRemoved, oStartTime, oEndTime);
				}
				else if (checkFirst == 0)
				{
					printf("TXT: Consumer %d, Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *(int *)cpuID, *(int *)processRemoved->pPID, processRemoved->iPriority, processRemoved->iPreviousBurstTime, processRemoved->iRemainingBurstTime);
					printProcessSVG(*(int *)cpuID, processRemoved, oStartTime, oEndTime);
				}
			}
			/*if priority is 0-15*/
			else if (processRemoved->iPriority < MAX_PRIORITY / 2)
			{
				if (checkFirst == 1)
				{
					printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %ld\n", *(int *)cpuID, *(int *)processRemoved->pPID, processRemoved->iPriority, processRemoved->iPreviousBurstTime, processRemoved->iRemainingBurstTime, rt);
					printProcessSVG(*(int *)cpuID, processRemoved, oStartTime, oEndTime);
				}
				else if (checkFirst == 0)
				{
					printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *(int *)cpuID, *(int *)processRemoved->pPID, processRemoved->iPriority, processRemoved->iPreviousBurstTime, processRemoved->iRemainingBurstTime);
					printProcessSVG(*(int *)cpuID, processRemoved, oStartTime, oEndTime);
				}
			}
			printf("TXT: Admitted: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *(int *)processRemoved->pPID, processRemoved->iPriority, processRemoved->iPreviousBurstTime, processRemoved->iRemainingBurstTime);
		}
	}
}


/*----------------------------------------------------------------------------PRIORITY BOOSTER--------------------------------------------------------------------------------------------*/
/**
 * runs as defined
 * move pcb from low priority queue into high prioriry queue
*/
void *f_booster(void * arg)
{
	int trackPri = 0;
	while (1)
	{
		sem_wait(&ready_sem_sync);
		/*if priority is 17-31*/
		for (trackPri = MAX_PRIORITY / 2 + 1; trackPri < MAX_PRIORITY; trackPri++)
		{
			if (arr_ready_head[trackPri])
			{
				/*remove 17-31*/
				struct process *p = removeFirst(&arr_ready_head[trackPri], &arr_ready_tail[trackPri]);
				/*put them at the end of readyqueue*/
				addLast(p, &arr_ready_head[MAX_PRIORITY / 2], &arr_ready_tail[MAX_PRIORITY / 2]);
				printf("TXT: Boost: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *(int *)p->pPID, p->iPriority, p->iPreviousBurstTime, p->iRemainingBurstTime);
			}
		}
		sem_post(&ready_sem_sync);
		usleep(BOOST_INTERVAL);
	}
}


/*--------------------------------------------------------------------------TERMINATION DAEMON---------------------------------------------------------------------------------*/
/**
 * runs as defined
 * remove process from terminated queue
 * remove process from process table
 * add pid back to free list
*/
void *f_termination(void * arg)
{
	while (1)
	{
		if (ter_head)
		{
			usleep(TERMINATION_INTERVAL);

			pthread_mutex_lock(&terQ);
			/*remove process from termination queue*/
			struct process *process = removeFirst(&ter_head, &ter_tail);
			/*get its pid*/
			int PID = *(int *)process->pPID;
			/*free this process on process table*/
			arr_processTable[arr_pids[PID]] = NULL;
			pthread_mutex_unlock(&terQ);

			/*track how many processes you have been freed*/
			processRemoved++;

			pthread_mutex_lock(&mutex_pcb);
			/*add this free pid back to free pid list*/
			addLast((void *)&arr_pids[PID], &free_pid_head, &free_pid_tail);
			
			//if this is the end of free list
			if (free_pid_head->pNext == NULL)
			{
				sem_post(&sync_sem);
				
			}
			pthread_mutex_unlock(&mutex_pcb);
			
		}
		/*if this is the last one process*/
		if (processRemoved == NUMBER_OF_PROCESSES)
		{
			double rt_avg = (double)cw_ResponseTime / NUMBER_OF_PROCESSES;
			double tr_avg = (double)cw_turnAroundTime / NUMBER_OF_PROCESSES;
			printf("TXT: Average response time = %lf, Average turnaround time = %lf\n", rt_avg, tr_avg);
			printFootersSVG();
			exit(1);
		}
	}
}


/*----------------------------------------------------------------------------DECLARE SVG SURROUNDING FUNCTIONS--------------------------------------------------------------------*/
void printHeadersSVG()
{
	printf("SVG: <!DOCTYPE html>\n");
	printf("SVG: <html>\n"); 
	printf("SVG: <body>\n");
	printf("SVG: <svg width=\"10000\" height=\"1100\">\n");
}

void printProcessSVG(int iCPUId, struct process *pProcess, struct timeval oStartTime, struct timeval oEndTime)
{
	int iXOffset = getDifferenceInMilliSeconds(oBaseTime, oStartTime) + 30;
	int iYOffsetPriority = (pProcess->iPriority + 1) * 16 - 12;
	int iYOffsetCPU = (iCPUId - 1) * (480 + 50);
	int iWidth = getDifferenceInMilliSeconds(oStartTime, oEndTime);
	printf("SVG: <rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"8\" style=\"fill:rgb(%d,0,%d);stroke-width:1;stroke:rgb(255,255,255)\"/>\n", iXOffset /* x */, iYOffsetCPU + iYOffsetPriority /* y */, iWidth, *(pProcess->pPID) - 1 /* rgb */, *(pProcess->pPID) - 1 /* rgb */);
}

void printPrioritiesSVG()
{
	for (int iCPU = 1; iCPU <= NUMBER_OF_CPUS; iCPU++)
	{
		for (int iPriority = 0; iPriority < MAX_PRIORITY; iPriority++)
		{
			int iYOffsetPriority = (iPriority + 1) * 16 - 4;
			int iYOffsetCPU = (iCPU - 1) * (480 + 50);
			printf("SVG: <text x=\"0\" y=\"%d\" fill=\"black\">%d</text>", iYOffsetCPU + iYOffsetPriority, iPriority);
		}
	}
}

void printRasterSVG()
{
	for (int iCPU = 1; iCPU <= NUMBER_OF_CPUS; iCPU++)
	{
		for (int iPriority = 0; iPriority < MAX_PRIORITY; iPriority++)
		{
			int iYOffsetPriority = (iPriority + 1) * 16 - 8;
			int iYOffsetCPU = (iCPU - 1) * (480 + 50);
			printf("SVG: <line x1=\"%d\" y1=\"%d\" x2=\"10000\" y2=\"%d\" style=\"stroke:rgb(125,125,125);stroke-width:1\" />", 16, iYOffsetCPU + iYOffsetPriority, iYOffsetCPU + iYOffsetPriority);
		}
	}
}

void printFootersSVG()
{
	printf("SVG: Sorry, your browser does not support inline SVG.\n");
	printf("SVG: </svg>\n");
	printf("SVG: </body>\n");
	printf("SVG: </html>\n");
}