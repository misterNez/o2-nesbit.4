//Process control block

#ifndef PCT_H_
#define PCT_H_

typedef struct
{
   long pid;
   int priority;
   int cpu_time;
   int total_time;
   int burst_time;
   int startSec;
   int startNS;
   int launched;
   int used;
} PCB;

#endif
