//Process control block

#ifndef PCT_H_
#define PCT_H_

typedef struct
{
   long pid;
   int priority;
   int cpu_time;
   int total_sec;
   int total_nano;
   int burst_time;
   int running;
   int ready;
   int duration;
   int done;
   int r;
   int s;
} PCB;

#endif
