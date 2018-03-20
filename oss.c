/* Created by: Nick Nesbit
 * Date: 3/19/2018
 * Process Scheduling and OS Simulator
 * CS 4760 Project 4
 * oss.c
*/

//Includes
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include "timer.h"
#include "pct.h"
#include "message.h"

//Global variable to flag for termination
volatile sig_atomic_t term = 0;

//Global variable for max processes
int maxProc = 18;

//Function prototypes
void display_help(char* prog);
void handle_signal(int sig);

//Start of main program
int main(int argc, char* argv[]) {

   //Set up signal handling
   sigset_t mask;
   sigfillset(&mask);
   sigdelset(&mask, SIGINT);
   sigdelset(&mask, SIGALRM);
   sigdelset(&mask, SIGTERM);
   sigprocmask(SIG_SETMASK, &mask, NULL);
   signal(SIGINT, handle_signal);
   signal(SIGALRM, handle_signal);

   //Shared memory timer
   Timer* timer;
   key_t key = ftok("/tmp", 35);
   int shmtid = shmget(key, sizeof(Timer), IPC_CREAT | 0666);
   timer = shmat(shmtid, NULL, 0);
   timer->secs = 0;
   timer->nanos = 0; 

   //Shared memory process control table
   PCB* pct;
   key = ftok("/tmp", 50);
   int shmpid = shmget(key, maxProc * sizeof(PCB), IPC_CREAT | 0666);
   pct = shmat(shmpid, NULL, 0);
   int i;
   for (i = 0; i < maxProc; i++)
      pct[i].used = 0;
 
   //Message queue
   struct msg_struc message;
   key = ftok("/tmp", 65);
   int msgid = msgget(key, IPC_CREAT | 0666);

   //Variables for getopt
   int c = 0;
   int hFlag = 0;
   int lFlag = 0;
   int tFlag = 0;
   extern char *optarg;
   extern int optind, optopt, opterr;

   //Default utility variables
   char* filename = "nesbitP3.log";
   int termTime = 3;

   //Check for command line arguments
   while ((c = getopt(argc, argv, "hl:t:")) != -1) {
      switch(c) {
         //Help
         case 'h':
            if (hFlag == 0) {
               display_help(argv[0]);
               hFlag++;
            }
            break;
         //Set log filename
         case 'l':
            if (lFlag == 0) {
               filename = optarg;
               printf("%s: filename set to: %s\n", argv[0], filename);
               lFlag++;
            }
            break;
         //Set termination time
         case 't':
            if (tFlag == 0) {
               termTime = atoi(optarg);
               printf("%s: termination time set to: %d\n", argv[0], termTime);
               tFlag++;
            }
            break;
      }
   }

   //Start termination timer
   alarm(termTime);

   //Seed random number generator
   srand(getpid());

   //Variables to store time until launching next process
   unsigned int secs;
   unsigned int nanos;
   unsigned int nextSec;
   unsigned int nextNS;

   //Local variables
   int total = 0;     //Total number of spawned processes
   int count = 0;     //Current number of spawned processes
   int index = 0;     //Index of process control table
   pid_t pid;         //PID variable
   int status;        //Used for status info from child
   FILE* log;	      //File variable

   //Constant time constraints
   const int maxTimeBetweenNewProcsNS = 500000000;
   const int maxTimeBetweenNewProcsSecs = 1;

   printf("%s: Computing... log filename: %s, termination time: %d\n", argv[0], filename, termTime);

   //Main master loop
   while ( (total < 100) && (term == 0) ) {

      //If resources allow another process
      if (count < 18) {
         secs = rand() % maxTimeBetweenNewProcsSecs;
         nanos = rand() % maxTimeBetweenNewProcsNS;
         nextSec = secs + timer->secs;
         nextNS = nanos + timer->nanos;
         while (nextNS > 1000000000) {
            nextNS -= 1000000000;
            nextSec++;
         }

         //Fork a new child and increment total
         for (i = 0; i < maxProc; i++) {
            if (pct[i].used == 0) {
               index = i;
               break;
            }
         }
                
         pid = fork();
         total++;
         count++;

         switch(pid) {

            //Fork error
            case -1:
               fprintf(stderr, "%s: Error: Failed to fork slave process\n", argv[0]);
               count--;
               pct[index].used = 0;
               break;

            //Child process
            case 0:
               //Start of a critical section
               msgrcv(msgid, &message, sizeof(message), (long)getpid(), 0);
               pct[index].launched = 1;

               //Print message to file
               snprintf(message.str, sizeof(message), "Master: Creating new child pid %ld at my time %d.%d\n",
                                                                    (long)getpid(), timer->secs, timer->nanos);
               log = fopen(filename, "a");
               fprintf(log, message.str);
               fclose(log);

               //printf("%s: Attempting to exec child process %ld\n", argv[0], (long)getpid());

               //Exit critical section
               message.type = 2;
               msgsnd(msgid, &message, sizeof(message), 0);

               //Execute the user program
               char ind[3];
               sprintf(ind, "%d", index);
               char* args[] = {"./user", ind, NULL};
               if (execv(args[0], args) == -1)
                  fprintf(stderr, "%s: Error: Failed to exec child process %ld\n", argv[0], (long)getpid());

               //Exit if exec error
               exit(1);

            //Parent process
            default:
                //Initialize the process block
                pct[index].pid = (long)pid;
         	pct[index].priority = 0;
                pct[index].cpu_time = 0;
                pct[index].total_time = 0;
                pct[index].burst_time = 0;
                pct[index].startSec = nextSec;
                pct[index].startNS = nextNS;
                pct[index].launched = 0;
                pct[index].used = 1;

               for (i = 0; i < maxProc; i++) {
                  if (pct[i].used == 1) {
                     if (timer->secs >= pct[i].startSec && timer->nanos >= pct[i].startNS && pct[i].launched == 0) {
                        message.type = (long)pct[i].pid;
                        msgsnd(msgid, &message, sizeof(message), 0);
                        msgrcv(msgid, &message, sizeof(message), 2, 0);
                     }
                  }
               }

               //Enter critical section if message received from user process
               if ( (msgrcv(msgid, &message, sizeof(message), (long)getpid(), IPC_NOWAIT) != -1) ) {
                  //Write message to file
                  log = fopen(filename, "a");
                  fprintf(log, message.str);
                  fclose(log);
                  pid = wait(&status);
                  for (i = 0; i < maxProc; i++) {
                     if ((long)pct[i].pid == (long)pid) {
                        pct[i].used = 0;
                        count--;
                        //printf("%s: Process %ld finished(1). %d child processes running.\n", argv[0], (long)pid, count);
                        break;
                     }
                  }
               }

               //Increment the timer 100 nanos and adjust
               timer->nanos += 100;
               while (timer->nanos >= 1000000000) {
                  timer->nanos -= 1000000000;
                  timer->secs++;
               }
                  
               break;
         }
         //End of switch pid statement
      }

      //Else If max number of children is reached
      else {
         for (i = 0; i < maxProc; i++) {
            if (pct[i].used == 1) {
               if (timer->secs >= pct[i].startSec && timer->nanos >= pct[i].startNS && pct[i].launched == 0) {
                  message.type = (long)pct[i].pid;
                  msgsnd(msgid, &message, sizeof(message), 0);
                  msgrcv(msgid, &message, sizeof(message), 2, 0);
               }
            }
         }
         //Receive message and write to file
         msgrcv(msgid, &message, sizeof(message), (long)getpid(), 0);
         log = fopen(filename, "a");
         fprintf(log, message.str);
         fclose(log);

         //Increment the timer 100 nanos and adjust
         timer->nanos += 100;
         while (timer->nanos >= 1000000000) {
            timer->nanos -= 1000000000;
            timer->secs++;
         }

         //Decrement the count
         pid = wait(&status);
         for (i = 0; i < maxProc; i++)
            if ((long)pct[i].pid == (long)pid) {
               pct[i].used = 0;
               count--;
               break;
            }
         //printf("%s: Process %ld finished(2). %d child processes running.\n", argv[0], (long)pid, count);
      }
      //End of if/else statement
   }
   //End of master while loop

   //For any remaining messages
   for (i = 0 ; i < count; i++) {
      //Receive message and write to file
      msgrcv(msgid, &message, sizeof(message), (long)getpid(), 0);
      log = fopen(filename, "a");
      fprintf(log, message.str);
      fclose(log);
     
      //Increment the timer 100 nanos and adjust
      timer->nanos += 100;
      while (timer->nanos >= 1000000000) {
         timer->nanos -= 1000000000;
         timer->secs++;
      }
   } 

   //Wait for all children to finish
   while ( (pid = wait(&status)) > 0) {
      pct[i].used = 0;
      count--;
      //printf("%s: Process %ld finished(3). %d child processes running.\n", argv[0], (long)pid, count);
   }

   //Report cause of termination
   if (term == 1)
      printf("%s: Ended because of user interrupt\n", argv[0]);
   else if (term == 2)
      printf("%s: Ended because of timeout\n", argv[0]);
   else if (total == 100) 
      printf("%s: Ended because total = 100\n", argv[0]);
   else
      printf("%s: Termination cause unknown\n", argv[0]);

   //Print info
   printf("%s: Program ran for %d.%d and generated %d user processes\n", argv[0], timer->secs, timer->nanos, total);

   //Free allocated memory
   shmdt(timer);
   shmctl(shmtid, IPC_RMID, NULL);
   shmdt(pct);
   shmctl(shmpid, IPC_RMID, NULL);
   msgctl(msgid, IPC_RMID, NULL);

   //End program
   return 0;
}
//End of main program


/****************************************
* Function definitions                  *
****************************************/

//display_help
void display_help(char* prog) {
   printf("%s:\tHelp: Operating Systems Project 4\n"
           "\tProcess Scheduling OS Simulator\n"
           "\tTo run: ./oss (default values set)\n"
           "\tOptions:\n"
           "\t\t-h : help menu\n"
           "\t\t-l [filename]: set the name of the produced log file (default is nesbitP3.log)\n"
           "\t\t-t [integer]: set timeout in seconds (default is 3)\n"
           "\tExample: ./oss -l file.log -t 30\n", prog);
}

//handle_signal
void handle_signal(int sig) {
   printf("./oss: Parent process %ld caught signal: %d. Cleaning up and terminating.\n", (long)getpid(), sig);
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
