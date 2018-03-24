/* Created by: Nick Nesbit
 * Date: 3/19/2018
 * Process Scheduling and OS Simulator
 * CS 4760 Project 4
 * user.c
*/

//Includes
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include "timer.h"
#include "pct.h"
#include "message.h"

//Global variable to flag termination
volatile sig_atomic_t term = 0;

//Function prototypes
void handle_signal(int sig);

//Start of user program
int main(int argc, char* argv[]) {
   //Setup signal handling
   sigset_t mask;
   sigfillset(&mask);
   sigdelset(&mask, SIGUSR1);
   sigdelset(&mask, SIGUSR2);
   sigdelset(&mask, SIGTERM);
   sigprocmask(SIG_SETMASK, &mask, NULL);
   signal(SIGUSR1, handle_signal);
   signal(SIGUSR2, handle_signal);

   //Shared memory for timer
   Timer* timer;
   key_t key = ftok("/tmp", 35); 
   int shmtid = shmget(key, sizeof(Timer), 0666);
   timer = shmat(shmtid, NULL, 0);

   //Shared memory for process control table
   int maxProc = 18;
   PCB* pct;
   key = ftok("/tmp", 50);
   int shmpid = shmget(key, maxProc * sizeof(PCB), 0666);
   pct = shmat(shmpid, NULL, 0);

   //Mesage queue
   struct msg_struc message;
   key = ftok("/tmp", 65);
   int msgid = msgget(key, 0666);
   
   //Seed random generation
   srand(getpid());
   int ran;
   int r = 0;
   int s = 0;
   int p = 0;

   //Iteration variable
   int round = 0;

   //Index variable
   int index = atoi(argv[1]);

   //Variable used to flag termination
   int done = 0;
 
   //Variables used to record time
   int start;
   int end;

   //Critical section loop
   while ( done == 0 && term == 0 ) {

      //Receive message from master
      msgrcv(msgid, &message, sizeof(message), (long)pct[index].pid, 0);

      //Increment round
      round++;
      if (round == 1)
         start = timer->secs*1000000000 + timer->nanos;    //Set the start time at round 1

      //Small change of termination
      ran = rand()%499 + 1;
      if ( ran < 2 ) {
         pct[index].burst_time = rand()%(pct[index].burst_time - 2) + 1;

         if ( (pct[index].burst_time + pct[index].cpu_time) >= pct[index].duration) {
            pct[index].burst_time = pct[index].duration - pct[index].cpu_time;
         }

         pct[index].cpu_time += pct[index].burst_time;
         pct[index].done = 1;
         timer->nanos += pct[index].burst_time;
         while (timer->nanos > 1000000000) {
            timer->nanos -= 1000000000;
            timer->secs++;
         }
         end = timer->secs*1000000000 + timer->nanos;
         int childNans = end - start;
         int childSecs = 0;

         while (childNans >= 1000000000) {
            childNans -= childNans;
            childSecs++;
         }

         pct[index].total_sec = childSecs;
         pct[index].total_nano = childNans;
         
         //snprintf(message.str, sizeof(message.str), "OSS: Child PID %ld is terminating at time %d.%d after running for "
                  //"%d.%d out of %d.%d because of random termination\n", 
                  //pct[index].pid, timer->secs, timer->nanos, 0, pct[index].cpu_time, pct[index].total_sec, pct[index].total_nano);
         
         message.type = (long)getppid();
         msgsnd(msgid, &message, sizeof(message), 0);
         shmdt(timer);
         shmdt(pct);
         return -1;
      }

      ran = rand()%99 + 1;
      //Not blocked
      if ( ran >= 20 ) {
         if ( (pct[index].burst_time + pct[index].cpu_time) >= pct[index].duration) {
            pct[index].burst_time = pct[index].duration - pct[index].cpu_time;
            pct[index].done = 1;
            //pct[index].cpu_time += pct[index].burst_time;
            done = 1;
         }
         timer->nanos += pct[index].burst_time;
         while (timer->nanos > 1000000000) {
            timer->nanos -= 1000000000;
            timer->secs++;
         }
         pct[index].cpu_time += pct[index].burst_time;
         //snprintf(message.str, sizeof(message.str), "OSS: Child PID %ld ran for %d.%d\n", pct[index].pid, 0, pct[index].burst_time);

         message.type = (long)getppid();
         msgsnd(msgid, &message, sizeof(message), 0);
      }
      //Blocked
      else {
         pct[index].ready = 0;
         r = rand()%5;
         s = rand()%1000;
         pct[index].burst_time = rand()%(pct[index].burst_time - 2) + 1;
         if ( (pct[index].burst_time + pct[index].cpu_time) >= pct[index].duration) {
            pct[index].burst_time = pct[index].duration - pct[index].cpu_time;
            pct[index].done = 1;
            //pct[index].cpu_time += pct[index].burst_time;
            done = 1;
         }
         timer->nanos += pct[index].burst_time;
         while (timer->nanos > 1000000000) {
            timer->nanos -= 1000000000;
            timer->secs++;
         }
         pct[index].cpu_time += pct[index].burst_time;
         pct[index].s = r + timer->secs;
         pct[index].s = s + timer->nanos;
         while (pct[index].s >= 1000000000) {
            pct[index].s -= 1000000000;
            pct[index].r++;
         }
         //snprintf(message.str, sizeof(message.str), "OSS: Child PID %ld blocked at time %d.%d for %d.%d, ran for %d.%d\n",
            //pct[index].pid, timer->secs, timer->nanos, r, s, 0, pct[index].burst_time);

         message.type = (long)getppid();
         msgsnd(msgid, &message, sizeof(message), 0);
      }
   }

   //Calculate the runtime
   end = timer->secs*1000000000 + timer->nanos;
   int childNans = end - start;
   int childSecs = 0;

   //Adjust the run time
   while (childNans >= 1000000000) {
      childNans -= childNans;
      childSecs++;
   }

   pct[index].total_sec = childSecs;
   pct[index].total_nano = childNans;
 
   //Create the log mesage
   //snprintf(message.str, sizeof(message.str), "OSS: Child PID %ld is terminating at time %d.%d because it reached %d.%d, lived for time %d.%d\n", 
            //pct[index].pid, timer->secs, timer->nanos, 0, pct[index].duration, pct[index].total_sec, pct[index].total_nano);

   //Send message(to parent)
   //message.type = (long)getppid();
   //msgsnd(msgid, &message, sizeof(message), 0);

   //Detach from shared memory
   shmdt(timer);
   shmdt(pct);
  
   //End of program
   return 0;
}
//End of user program


/***********************************
* Function definitions             *
***********************************/

//handle_signal 
void handle_signal(int sig) {
   printf("./user: Child process %ld caught signal: %d. Terminating...\n", (long)getpid(), sig);
   switch(sig) {
      case SIGINT:
         kill(0, SIGUSR1);
         term = 1;
         break;
      case SIGALRM:
         kill(0, SIGUSR2);
         term = 2;
         break;
   }
}
