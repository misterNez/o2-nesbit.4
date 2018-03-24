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
const int maxProc = 18;

//Global arrays for process queues
long roundRobin[18];
long feedbck_1[18];
long feedbck_2[18];
long feedbck_3[18];
long blocked[18];

//Array to hold time slices
int quantum[4] = {2000000, 4000000, 8000000, 16000000};

//Global utility variables. Used for for loops.
int i;
int j;
int k;

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
   for (i = 0; i < maxProc; i++)
      pct[i].ready = -1;

   //Initialize process queues
   for ( i = 0; i < maxProc; i++ ) {
      roundRobin[i] = 0;
      feedbck_1[i] = 0;
      feedbck_2[i] = 0;
      feedbck_3[i] = 0;
      blocked[i] = 0;
   }
 
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
   char* filename = "nesbitP4.log";
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
   unsigned int ran;

   //Variables to store time until launching next process
   unsigned int secs;
   unsigned int nanos;

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

   log = fopen(filename, "w");

   //Main master loop
   while ( term == 0 || count > 0) {

      //Generate random time to spawn process
      secs = rand() % maxTimeBetweenNewProcsSecs;
      nanos = rand() % maxTimeBetweenNewProcsNS;
      
      //Adjust as needed
      timer->secs += secs;
      timer->nanos += nanos; 
      while (timer->nanos > 1000000000) {
         timer->nanos -= 1000000000;
         timer->secs++;
      }

      //If resources allow another process
      if ( count < 18 && total < 100 ) {

         //Find open process control block
         for (i = 0; i < maxProc; i++) {
            if (pct[i].ready == -1) {
               index = i;
               break;
            }
         }
                
         //Fork and increment total/count
         pid = fork();
         total++;
         count++;

         //Switch PID
         switch(pid) {

            //Fork error
            case -1:
               fprintf(stderr, "%s: Error: Failed to fork slave process\n", argv[0]);
               count--;
               break;

            //Child process
            case 0:
               //Start of a critical section
               msgrcv(msgid, &message, sizeof(message), 1, 0);

               printf("%s: Attempting to exec child process %ld\n", argv[0], (long)getpid());

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

               //Determine class and initial time slice
               ran = rand()%99 + 1;

               //Real-time:
               if ( ran < 3 ) {
                  pct[index].priority = 0;
                  for ( i = 0; i < maxProc; i++) {
                     if (roundRobin[i] == 0) {
                        roundRobin[i] = pct[index].pid;
                        pct[index].burst_time = quantum[0];
                        break;
                     }
                  }
               }
               //Normal:
               else {
                  pct[index].priority = 1;
                  for ( i = 0; i < maxProc; i++) {
                     if (feedbck_1[i] == 0) {
                        feedbck_1[i] = pct[index].pid;
                        pct[index].burst_time = quantum[1];
                        break;
                     }
                  }
               }   
 
               //Generate random duration
               pct[index].duration = rand()%99999998 + 1;

               //Initialize time members
               pct[index].cpu_time = 0;
               pct[index].total_sec = 0;
               pct[index].total_nano = 0;

               //Initialize the running and ready states
               pct[index].running = 0;
               pct[index].ready = 1;
               pct[index].done = 0;

               //Initialize block variables
               pct[index].r = 0;
               pct[index].s = 0;

               //Write to log
               snprintf(message.str, sizeof(message), "OSS: Generating process with PID %ld and putting it in queue %d at time %d.%d\n",
                                                                    pct[index].pid, pct[index].priority, timer->secs, timer->nanos);
               fprintf(log, message.str);

               //Confirm the child process execution
               message.type = 1;
               msgsnd(msgid, &message, sizeof(message), 0);
               msgrcv(msgid, &message, sizeof(message), 2, 0);
               break;
         } 
         //End of switch statement
      }
      //End of if statement

         
      //Increment timer for scheduling work
      timer->nanos += rand()%9900 + 100;
      while (timer->nanos > 1000000000) {
         timer->nanos -= 1000000000;
         timer->secs++;
      }
  
      //Check blocked queue
 
      //Schedule round robin queue
      if (roundRobin[0] != 0) {			//Get the index number
         for (i = 0; i < maxProc; i++) {
            if (pct[i].pid == roundRobin[0]) {
               break;
            }
         }
         //Set time burst
         pct[i].burst_time = quantum[0];

         //Write to log
         snprintf(message.str, sizeof(message), "OSS: Dispaching process with PID %ld from queue %d at time %d.%d\n",
                                                                    pct[i].pid, pct[i].priority, timer->secs, timer->nanos);
         fprintf(log, message.str);

         //Let the child process run
         message.type = (long)roundRobin[0];
         msgsnd(msgid, &message, sizeof(message), 0);

         //Receive message
         msgrcv(msgid, &message, sizeof(message), (long)getpid(), 0);

         //If done
         if (pct[i].done == 1) {
            waitpid(pct[i].pid, &status, 0);
            snprintf(message.str, sizeof(message), "OSS: Process with PID %ld has finished at time %d.%d after running %d nanoseconds\n",
                                                                    pct[i].pid, timer->secs, timer->nanos, pct[i].cpu_time);
            fprintf(log, message.str);
            roundRobin[0] = 0;
            pct[i].ready = -1;
            count--;
         }
         //Else if blocked
         else if ( pct[i].ready = 0 ) {
            snprintf(message.str, sizeof(message.str), "OSS: Process with PID %ld blocked at time, ran for %d nanoseconds\n",
                                                                    pct[i].pid, timer->secs, timer->nanos, pct[index].burst_time);
            fprintf(log, message.str);
         }
         //Else if still running
         else {
            snprintf(message.str, sizeof(message), "OSS: Recieving that process with PID %ld ran for %d nanoseconds\n",
                                                                    pct[i].pid, pct[i].burst_time );
            fprintf(log, message.str);
         }

         //Update the priority queues
         long temp = roundRobin[0];
         for ( i = 0; i < (maxProc-1); i++) {
            roundRobin[i] = roundRobin[i+1];
         }
         roundRobin[maxProc-1] = 0;
         for ( i = 0; i < maxProc; i++) {
            if (roundRobin[i] == 0) {
               roundRobin[i] = temp;
               break;
            }
         }  
      }

      //Schedule high-priority queue
      else if (feedbck_1[0] != 0) {		//Get the index number
         for (i = 0; i < maxProc; i++) {
            if (pct[i].pid == feedbck_1[0]) {
               break;
            }
         }

         //Set the burst time
         pct[i].burst_time = quantum[1];
 
         //Write to log
         snprintf(message.str, sizeof(message), "OSS: Dispaching process with PID %ld from queue %d at time %d.%d\n",
                                                                    pct[i].pid, pct[i].priority, timer->secs, timer->nanos);
         fprintf(log, message.str);

         //Let the child process run
         message.type = (long)feedbck_1[0];
         msgsnd(msgid, &message, sizeof(message), 0);

         //Receive message
         msgrcv(msgid, &message, sizeof(message), (long)getpid(), 0);
      
         //If done
         if (pct[i].done == 1) {
            waitpid(pct[i].pid, &status, 0);
            snprintf(message.str, sizeof(message), "OSS: Process with PID %ld has finished at time %d.%d after running %d nanoseconds\n",
                                                                    pct[i].pid, timer->secs, timer->nanos, pct[i].cpu_time);
            fprintf(log, message.str);
            feedbck_1[0] = 0;
            pct[i].ready = -1;
            count--;
         }
         //Else if blocked
         else if ( pct[i].ready = 0 ) {
            pct[i].priority++;
            snprintf(message.str, sizeof(message.str), "OSS: Process with PID %ld blocked at time, ran for %d nanoseconds\n",
                                                                    pct[i].pid, timer->secs, timer->nanos, pct[index].burst_time);
            fprintf(log, message.str);
         }
         //Else if still running
         else {
            pct[i].priority++;
            snprintf(message.str, sizeof(message), "OSS: Recieving that process with PID %ld ran for %d nanoseconds\n",
                                                                    pct[i].pid, pct[i].burst_time );
            fprintf(log, message.str);

            snprintf(message.str, sizeof(message), "OSS: Putting process with PID %ld into queue %d\n",
                                                                    pct[index].pid, pct[index].priority);
            fprintf(log, message.str);
         }

         //Update the priority queues
         long temp = feedbck_1[0];
         for ( i = 0; i < (maxProc-1); i++) {
            feedbck_1[i] = feedbck_1[i+1];
         }
         feedbck_1[maxProc-1] = 0;
         for ( i = 0; i < maxProc; i++) {
            if (feedbck_2[i] == 0) {
               feedbck_2[i] = temp;
               break;
            }
         }
      }

      //Schedule medium-priority queue
      else if (feedbck_2[0] != 0) {
         for (i = 0; i < maxProc; i++) {	//Get the index number
            if (pct[i].pid == feedbck_2[0]) {
               break;
            }
         }
     
         //Set the time burst
         pct[i].burst_time = quantum[2];

         //Write to log
         snprintf(message.str, sizeof(message), "OSS: Dispaching process with PID %ld from queue %d at time %d.%d\n",
                                                                    pct[i].pid, pct[i].priority, timer->secs, timer->nanos);
         fprintf(log, message.str);

         //Let the child process run
         message.type = (long)feedbck_2[0];
         msgsnd(msgid, &message, sizeof(message), 0);

         //Receive message
         msgrcv(msgid, &message, sizeof(message), (long)getpid(), 0);

         //If done
         if (pct[i].done == 1) {
            waitpid(pct[i].pid, &status, 0);
            snprintf(message.str, sizeof(message), "OSS: Process with PID %ld has finished at time %d.%d after running %d nanoseconds\n",
                                                                    pct[i].pid, timer->secs, timer->nanos, pct[i].cpu_time);
            fprintf(log, message.str);
            feedbck_2[0] = 0;
            pct[i].ready = -1;
            count--;
         }
         //Else if blocked
         else if ( pct[i].ready = 0 ) {
            pct[i].priority++;
            snprintf(message.str, sizeof(message.str), "OSS: Process with PID %ld blocked at time, ran for %d nanoseconds\n",
                                                                    pct[i].pid, timer->secs, timer->nanos, pct[index].burst_time);
            fprintf(log, message.str);
         }
         //Else if still running
         else {
            pct[i].priority++;
            snprintf(message.str, sizeof(message), "OSS: Recieving that process with PID %ld ran for %d nanoseconds\n",
                                                                    pct[i].pid, pct[i].burst_time );
            fprintf(log, message.str);

            snprintf(message.str, sizeof(message), "OSS: Putting process with PID %ld into queue %d\n",
                                                                    pct[index].pid, pct[index].priority);
            fprintf(log, message.str);
         }

         //Update the priority queues
         long temp = feedbck_2[0];
         for ( i = 0; i < (maxProc-1); i++) {
            feedbck_2[i] = feedbck_2[i+1];
         }
         feedbck_2[maxProc-1] = 0;
         for ( i = 0; i < maxProc; i++) {
            if (feedbck_3[i] == 0) {
               feedbck_3[i] = temp;
               break;
            }
         }
      }

      //Schedule low-priority queue
      else if (feedbck_3[0] != 0) {
         for (i = 0; i < maxProc; i++) {	//Get the index number
            if (pct[i].pid == feedbck_3[0]) {
               break;
            }
         }
 
         //Set the burst time
         pct[i].burst_time = quantum[3];

         //Write to log
         snprintf(message.str, sizeof(message), "OSS: Dispaching process with PID %ld from queue %d at time %d.%d\n",
                                                                    pct[i].pid, pct[i].priority, timer->secs, timer->nanos);
         fprintf(log, message.str);

         //Let the child process run
         message.type = (long)feedbck_3[0];
         msgsnd(msgid, &message, sizeof(message), 0);

         //Receive message
         msgrcv(msgid, &message, sizeof(message), (long)getpid(), 0);
 
         //If done
         if (pct[i].done == 1) {
            waitpid(pct[i].pid, &status, 0);
            snprintf(message.str, sizeof(message), "OSS: Process with PID %ld has finished at time %d.%d after running %d nanoseconds\n",
                                                                    pct[i].pid, timer->secs, timer->nanos, pct[i].cpu_time);
            fprintf(log, message.str);
            feedbck_3[0] = 0;
            pct[i].ready = -1;
            count--;
         }
         //Else if blocked
         else if ( pct[i].ready = 0 ) {
            snprintf(message.str, sizeof(message.str), "OSS: Process with PID %ld blocked at time, ran for %d nanoseconds\n",
                                                                    pct[i].pid, timer->secs, timer->nanos, pct[index].burst_time);
            fprintf(log, message.str);
         }
         //Else if still running
         else {
            snprintf(message.str, sizeof(message), "OSS: Recieving that process with PID %ld ran for %d nanoseconds\n",
                                                                    pct[i].pid, pct[i].burst_time );
            fprintf(log, message.str);
         }

         //Update the priority queues
         long temp = feedbck_3[0];
         for ( i = 0; i < (maxProc-1); i++) {
            feedbck_3[i] = feedbck_3[i+1];
         }
         feedbck_3[maxProc-1] = 0;
         for ( i = 0; i < maxProc; i++) {
            if (feedbck_3[i] == 0) {
               feedbck_3[i] = temp;
               break;
            }
         }
      }
   }
   //End of master while loop

   //Wait for all child processes to finish
   while ( (pid = wait(&status)) > 0) {
      count--;
      printf("%s: Process %ld finished. %d child processes running.\n", argv[0], (long)pid, count);
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

   //Close file
   fclose(log);

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
           "\t\t-l [filename]: set the name of the produced log file (default is nesbitP4.log)\n"
           "\t\t-t [integer]: set timeout in real seconds (default is 3)\n"
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
