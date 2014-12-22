#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>
#include "mesg.h"
#include "err.h"
#include "control.h"

#define MAX_LIST        10
#define MAX_CANDIDATES  10
#define CLIENTS_LIMIT   2
#define MAX_COMMITTEES  100

/* TO-DOs
 *  -> make error checking for pthread functions
 *  -> attr should be array!!
 */

/* globals */
int in_qid, report_qid, comout_qid, comin_qid;
int result_table[MAX_LIST][MAX_CANDIDATES];
int committee_reported[MAX_COMMITTEES];
int allowed_to_vote; 
int voted_with_invalid;
int voted;
int committees_processed;

/* data access control variables  */
pthread_mutex_t data_control;
DataAccessControl do_not_let_new_in;
DataAccessControl how_many_clients_in;
pthread_attr_t attr;

void exit_gracefully(int sig) {
    if (msgctl(in_qid, IPC_RMID, 0))
        syserr("msgctl in server: unable to free main in message queue");
    if (msgctl(report_qid, IPC_RMID, 0))
        syserr("msgctl in server: unable to free report message queue");
    if (msgctl(comout_qid, IPC_RMID, 0))
        syserr("msgctl in server: unable to free committee out message queue");
    if (msgctl(comin_qid, IPC_RMID, 0))
        syserr("msgctl in server: unable to free committee in message queue");
    pthread_mutex_destroy(&data_control);
    pthread_mutex_destroy(&do_not_let_new_in.mtx);
    pthread_cond_destroy(&do_not_let_new_in.cnd);
    pthread_mutex_destroy(&how_many_clients_in.mtx);
    pthread_cond_destroy(&how_many_clients_in.cnd);
    pthread_attr_destroy(&attr);
}

void *serve_committee(void *d) {
    Mesg mesg;
    int l;
    long *data_got_raw =(long*) d;
    /* data[0] - handled committee number
     * data[1] - my thread_no
     */
    int com_no = (int) data_got_raw[0];
    int thread_no = (int) data_got_raw[1];
    int allowed_to_vote_here = 0;
    int voted_here_with_invalid = 0;
    int processed_lines = 0;
    int voted_here = 0;
    if (DEBUG)
        printf("spawned handler for committee no %d, thread no %d\n",
                com_no, thread_no);

    /* send confirmation of handler creation or deny access */
    int give_access;
    pthread_mutex_lock(&data_control);
    if (committee_reported[com_no] > 0)
        give_access = 0;
    else {
        give_access = 1;
        committee_reported[com_no]++;
        committees_processed++;
    };
    pthread_mutex_unlock(&data_control);

    mesg.mesg_type = (long) com_no;
    if (give_access == 0) {
        /* deny access */
        mesg.data[0] = 0;
        if (msgsnd(comout_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
                        syserr("msgsnd in server committee handler thread");
        if (DEBUG)
            printf("access denied to %d in thread %d \n", com_no, thread_no);

    } else {
        /* get ready to receive data */ 
        mesg.data[0] = 1;
        mesg.data[1] = thread_no;
        /* send confirmation of handler allocation */
        if (msgsnd(comout_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
                        syserr("msgsnd in server committee handler thread");
        if (DEBUG)
            printf("access granted to %d in thread %d \n", com_no, thread_no);

        /* receive special first data:
         *     no of people allowed to vote
         *     no of people who actually voted */
        if ((l = msgrcv(comin_qid, &mesg, sizeof(mesg.data), 
                                                    com_no, 0)) <= 0)
                                            syserr("msgrcv in server");
        allowed_to_vote_here = mesg.data[1];
        voted_here_with_invalid = mesg.data[2];
        if (DEBUG)
            printf("received initial message with allowed = %d and"
                   " votes (with invalid) = %d\n", 
                    allowed_to_vote_here,
                    voted_here_with_invalid);
        pthread_mutex_lock(&data_control);
        allowed_to_vote += allowed_to_vote_here;
        voted_with_invalid += voted_here_with_invalid;
        pthread_mutex_unlock(&data_control);
        /* send confirmation */
        if (msgsnd(comout_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
                        syserr("msgsnd in server committee handler thread");

        int go_on = 1;
        /* while you did not get end of data message */
        while (go_on) {
            /* get and proccess message */
            if ((l = msgrcv(comin_qid, &mesg, sizeof(mesg.data), 
                                                        com_no, 0)) <= 0)
                syserr("msgrcv in server");
            if (mesg.data[0] == 0) {
                /* end of data */
                go_on = 0;
                /* send confirmation */ 
                mesg.data[0] = 0;
                mesg.data[1] = processed_lines;
                mesg.data[2] = voted_here;
                if (msgsnd(comout_qid, (char *) &mesg,
                            sizeof(mesg.data), 0) != 0)
                    syserr("msgsnd in server committee handler thread");
            } else {
                /* standard middle message */
                processed_lines++;
                pthread_mutex_lock(&data_control);
                result_table[mesg.data[1]][mesg.data[2]] += mesg.data[3];
                voted_here += mesg.data[3];
                voted += mesg.data[3];
                pthread_mutex_unlock(&data_control);
                if (DEBUG)
                    printf("got %d votes for list %d candidate no %d \n",
                            mesg.data[3], mesg.data[1], mesg.data[2]);

                /* send confirmation */
                if (msgsnd(comout_qid, (char *) &mesg,
                            sizeof(mesg.data), 0) != 0)
                    syserr("msgsnd in server committee handler thread");
            }
        }
    };
    /* push summary data to main memory ??  */
    

    /* end protocol */
    /* decrease number of clients handled */
    pthread_mutex_lock(&how_many_clients_in.mtx);
    how_many_clients_in.val--;
    pthread_cond_signal(&how_many_clients_in.cnd);
    pthread_mutex_unlock(&how_many_clients_in.mtx);

    if (DEBUG)
        printf("exiting handler for committee no %d\n",  com_no);
    pthread_exit(NULL);
}

void *serve_report(void *data) {
    if (DEBUG)
        printf("spawned handler for report \n");
    /* initalize data */
    Mesg mesg;
    /* confirm access to handler*/
    mesg.mesg_type = 2;
    mesg.data[0] = 1; /* access granted */
    if (msgsnd(report_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
        syserr("msgsnd in server committee handler thread");

    /* send data */
    /* no need for mutex - mutual exclusion already ensured */

    /* send no of processed committees data */
    mesg.mesg_type = 3;
    mesg.data[0] = committees_processed;
    mesg.data[1] = MAX_COMMITTEES;
    if (msgsnd(report_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
        syserr("msgsnd in server committee handler thread");

    /* send turnout data */
    mesg.mesg_type = 4;
    mesg.data[0] = allowed_to_vote;
    mesg.data[1] = voted;
    mesg.data[2] = voted_with_invalid - voted;
    if (msgsnd(report_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
        syserr("msgsnd in server committee handler thread");

    /* get info which list's data is needed */
    int com_no;
    if ((l = msgrcv(rep_qid, &mesg, sizeof(mesg.data), 2, 0)) <= 0)
        syserr("msgrcv in report");
    com_no = mesg.data[0];

    if (com_no) {
        /* just one list needed */
    } else {
        /* all list data needed */
    }

    /* end protocol */
    /* allow creation of new handlers */
    pthread_mutex_lock(&do_not_let_new_in.mtx);
    do_not_let_new_in.val = 0;
    pthread_cond_signal(&do_not_let_new_in.cnd);
    pthread_mutex_unlock(&do_not_let_new_in.mtx);

    if (DEBUG)
        printf("exiting handler for report \n");
    pthread_exit(NULL);
}

int main() 
{
    /* declare, initalize data */
    Mesg mesg;
    long pass[2];
    int i, j, l;
    pthread_t threads[CLIENTS_LIMIT];
    int err;
    for (i = 0; i < MAX_LIST; i++) 
        for (j = 0; j < MAX_CANDIDATES; j++)
            result_table[i][j] = 0;
    for (i = 0; i < MAX_COMMITTEES; i++)
        committee_reported[i] = 0;
    allowed_to_vote = 0; 
    voted_with_invalid = 0;
    voted = 0;
    committees_processed = 0;

    /* setup custom SIGKILL handling to free msg queue when exiting */
    if (signal(SIGINT, exit_gracefully) == SIG_ERR)
        syserr("procedure signal: in setting up process exiting procedure");

    if (DEBUG)
        printf("server up and accepting requests \n");

    /* start message queues */
    if ((in_qid = msgget(MAIN_IN_Q, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
        syserr("msgget in creating main request queue");
    if ((report_qid = msgget(REPORT_Q, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
        syserr("msgget in creating main request queue");

    if ((comin_qid = msgget(COMMITTEE_Q_IN, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
        syserr("msgget in creating main request queue");
    if ((comout_qid = msgget(COMMITTEE_OUT_Q,  0666 | IPC_CREAT | IPC_EXCL)) == -1)
        syserr("msgget in creating main request queue");

    /* set up all the data access control instruments */
    pthread_mutex_init(&data_control, NULL);
    pthread_mutex_init(&do_not_let_new_in.mtx, NULL);
    pthread_cond_init(&do_not_let_new_in.cnd, NULL);
    do_not_let_new_in.val = 0;
    pthread_mutex_init(&how_many_clients_in.mtx, NULL);
    pthread_cond_init(&how_many_clients_in.cnd, NULL);
    how_many_clients_in.val = 0;

    for (;;) {
        /* get message */
        if ((l = msgrcv(in_qid, &mesg, sizeof(mesg.data), 0L, 0)) <= 0)
                syserr("msgrcv in server");
        if (mesg.data[0] == 1) {
            /* new committee handler requested  */

            pthread_mutex_lock(&do_not_let_new_in.mtx);
            while (do_not_let_new_in.val != 0)
                pthread_cond_wait(&do_not_let_new_in.cnd,  
                                  &do_not_let_new_in.mtx);
            /* this _single_ main thread hanging on this conditional 
             * will be signaled by exiting Report */
            pthread_mutex_unlock(&do_not_let_new_in.mtx);

            pthread_mutex_lock(&how_many_clients_in.mtx);
            while (how_many_clients_in.val >= CLIENTS_LIMIT)
                pthread_cond_wait(&how_many_clients_in.cnd,
                                  &how_many_clients_in.mtx);  
            how_many_clients_in.val++;
            pass[0] = mesg.mesg_type;
            pass[1] = how_many_clients_in.val;
            pthread_mutex_unlock(&how_many_clients_in.mtx);
            if ((err = pthread_attr_init(&attr)) != 0)
                syserr("pthread_attr_init in creating committee handler");
           
            if ((err = pthread_create(&threads[how_many_clients_in.val - 1], 
                                      &attr, serve_committee, 
                                      (void *)pass)) != 0)
                   syserr("pthread_create in creating committee handler");
        } else if (mesg.data[0] == 2) {
            /* new report handler requested  */

            pthread_mutex_lock(&do_not_let_new_in.mtx);
            while (do_not_let_new_in.val != 0) 
                pthread_cond_wait( &do_not_let_new_in.cnd,  
                                   &do_not_let_new_in.mtx);
            /* this _single_ main thread hanging on this conditional 
             * will be signaled by exiting Report */
            /* report handler requested: block requests for new threads */
            do_not_let_new_in.val = 1;
            pthread_mutex_unlock(&do_not_let_new_in.mtx);

            /* CLAIM: 
             *  - at most 1 report handler past this point
             *  - maybe some committee handlers in
             *  - no one else can get in until do_not_let_new_in is released
             *    as _only one_ thread (main) creates new threads
             */

            /* create new thread for handling report */
            pthread_mutex_lock(&how_many_clients_in.mtx);
            while (how_many_clients_in.val > 0)
                pthread_cond_wait( &how_many_clients_in.cnd,
                                   &how_many_clients_in.mtx);
            /* this _single_ main thread hanging on this conditional 
             * will be signaled by exiting clients. The wait 
             * is finsihed because do_not_let_new_in is not allowing 
             * new committee handlers  */
            pthread_mutex_unlock(&how_many_clients_in.mtx);
            if ((err = pthread_attr_init(&attr)) != 0)
                syserr("pthread_attr_init in creating report handler");
           
           if ((err = pthread_create(&threads[0], 
                                     &attr, serve_report, (void*)0)) != 0)
                  syserr("pthread_create in creating report handler");
        }
    }
}
