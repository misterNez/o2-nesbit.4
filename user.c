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

   //Set random simulated duration
   int dur = rand()%1000000 + 1;

   //Workload variables
   int work;
   int total = 0;
   int round = 0;

   //Variable used to flag termination
   int done = 0;
 
   //Variable used to record start time 
   int start;

   //Critical section loop
   while (done == 0 && term == 0) {

      //Receive message (using type 3)
      msgrcv(msgid, &message, sizeof(message), 3, 0);

      //Increment round
      round++;
      if (round == 1)
         start = timer->secs*1000000000 + timer->nanos;   //Set the start time at round 1

      //Random amount of work
      work = rand()%200 + 1;

      //Adjust the work as needed
      if ( work >= dur) {		//If work is greater than duration
         work = dur;
         done = 1;
      }
      if ( (work + total) >= dur ) {   //If the total work is greater than or equal to duration
         work = dur - total;
         done = 1;
      }

      //Incrememnt timer
      timer->nanos += work;
      total += work;

      //Adjust the timer
      while (timer->nanos >= 1000000000) {
         timer->nanos -= 1000000000;
         timer->secs++;
      }

      //Cede to another procress if not finished
      if ( done == 0) {
          message.type = 3;					//Send message of type 1
          msgsnd(msgid, &message, sizeof(message), 0);
      }
   }

   //Calculate the runtime
   int end = timer->secs*1000000000 + timer->nanos;
   int childNans = end - start;
   int childSecs = 0;

   //Adjust the run time
   while (childNans >= 1000000000) {
      childNans -= childNans;
      childSecs++;
   }
 
   //Create the log mesage
   snprintf(message.str, sizeof(message.str), "Master: Child PID %ld is terminating at my time %d.%d because it reached %d.%d,"
                      " which lived for time %d.%d\n", (long)getpid(), timer->secs, timer->nanos, 0, dur, childSecs, childNans);

   //Send message(to parent)
   message.type = (long)getppid();
   msgsnd(msgid, &message, sizeof(message), 0);

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
