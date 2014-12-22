#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "err.h"
#include "mesg.h"

int main(int argc, char *argv[]) 
{
    /* declare data */
    Mesg mesg;
    int msg_qid, comout_qid, send_qid;
    int l;
    int nr_komisji;
    int processed_lines = 0;
    int voted_local = 0;
    int entitled_to_vote = 0;
    int voted_local_with_invalid = 0;
    if (argc != 2)
        syserr("komisja: wrong number of arguments specified");
    nr_komisji = atoi(argv[1]);

    /* get main queue id */
    if ((msg_qid = msgget(MAIN_IN_Q, 0)) == -1)
        syserr("msgget in komisja main queue");
    if ((comout_qid = msgget(COMMITTEE_OUT_Q, 0)) == -1)
        syserr("msgget in komisja committee out q");
    if ((send_qid = msgget(COMMITTEE_Q_IN, 0)) == -1)
        syserr("msgget in komisja committee out q");

    /* requesting handler creation from server by sending our commision number
     * by message type */
    mesg.mesg_type = nr_komisji; 
    mesg.data[0] = 1;  
    if (msgsnd(msg_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
        syserr("msgsnd in komisja");

    if ((l = msgrcv(comout_qid, &mesg, sizeof(mesg.data), nr_komisji, 0)) <= 0)
                        syserr("msgrcv in server");

    if (mesg.data[0] == 0) {
        /* handler denied */
        syserr("access denied");
        exit(5);
    } else {
        /* handler granted */
        mesg.data[0] = 1;

        /* send data begin:  */
        scanf("%d %d\n", &mesg.data[1], &mesg.data[2]);
        if (msgsnd(send_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
            syserr("msgsnd in komisja");
        entitled_to_vote = mesg.data[1];
        voted_local_with_invalid = mesg.data[2];
        /* wait for confirmation */
        if ((l = msgrcv(comout_qid, &mesg, sizeof(mesg.data), nr_komisji, 0)) <= 0)
                            syserr("msgrcv in server");
        /* send general data 
        * while you are getting info from stdin */
        while (!feof(stdin) && scanf("%d %d %d\n",
                                             &mesg.data[1],
                                             &mesg.data[2],
                                             &mesg.data[3])) {
            /* accumulate data */
            voted_local += mesg.data[3];
            processed_lines++;
            /* send further data  */
            if (msgsnd(send_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
                syserr("msgsnd in komisja");
            /* wait for confirmation */
            if ((l = msgrcv(comout_qid, &mesg, sizeof(mesg.data),
                            nr_komisji, 0)) <= 0)
                                syserr("msgrcv in server");

        }

        /* send final data  */
        mesg.data[0] = 0;
        if (msgsnd(send_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
            syserr("msgsnd in komisja");
        /* wait for confirmation with data summary  */
        if ((l = msgrcv(comout_qid, &mesg, sizeof(mesg.data), nr_komisji, 0)) <= 0)
                            syserr("msgrcv in server");
        /* check for correctness */
        assert(mesg.data[0] == 0);
        assert(mesg.data[1] == processed_lines);
        assert(mesg.data[2] == voted_local);
        /* write summary to stdout */
        printf("Przetworzonych wpisów: %d\n", processed_lines);
        printf("Uprawnionych do głosowania: %d\n", entitled_to_vote);
        printf("Głosów ważnych: %d\n", voted_local);
        printf("Głosów nieważnych: %d\n", voted_local_with_invalid - voted_local);
        float turnout = (float) voted_local_with_invalid / (float) entitled_to_vote;
        turnout *= 100;
        printf("Frekwencja w lokalu: %f%% \n", turnout);
        fflush(stdout);
    };

    /* free resources ?? */

    return 0;
}
