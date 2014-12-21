#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "mesg.h"
#include "err.h"

int main(int argc, char *argv[]) 
{
    /* declare data */
    Mesg mesg;
    int msg_qid, comout_qid, send_qid;
    int l;
    int nr_komisji;
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
    if (DEBUG)
        printf("in komisja no %d got msg_qid, it is %d\n", nr_komisji, msg_qid);

    /* requesting handler creation from server by sending our commision number
     * by message type */
    mesg.mesg_type = nr_komisji; 
    mesg.mesg_data[0] = 1;  
    if (DEBUG)
        printf("requesting handler from server \n");
    if (msgsnd(msg_qid, (char *) &mesg, sizeof(mesg.mesg_data), 0) != 0)
        syserr("msgsnd in komisja");

    if ((l = msgrcv(comout_qid, &mesg, sizeof(mesg.mesg_data), nr_komisji, 0)) <= 0)
                        syserr("msgrcv in server");

    if (mesg.mesg_data[0] == 0) {
        /* handler denied */
        syserr("access denied");
        exit(5);
    } else {
        /* handler granted */
        mesg.mesg_data[0] = 1;

        /* send data begin:  */
        scanf("%d %d\n", &mesg.mesg_data[1], &mesg.mesg_data[2]);
        if (msgsnd(send_qid, (char *) &mesg, sizeof(mesg.mesg_data), 0) != 0)
            syserr("msgsnd in komisja");
        /* wait for confirmation */
        if ((l = msgrcv(comout_qid, &mesg, sizeof(mesg.mesg_data), nr_komisji, 0)) <= 0)
                            syserr("msgrcv in server");
        printf("received confirmation\n");
        /* send general data 
        * while you are getting info from stdin */
        while (!feof(stdin) && scanf("%d %d %d\n",
                                             &mesg.mesg_data[1],
                                             &mesg.mesg_data[2],
                                             &mesg.mesg_data[3])) {
            /* send further data  */
            if (msgsnd(send_qid, (char *) &mesg, sizeof(mesg.mesg_data), 0) != 0)
                syserr("msgsnd in komisja");
            /* wait for confirmation */
            if ((l = msgrcv(comout_qid, &mesg, sizeof(mesg.mesg_data),
                            nr_komisji, 0)) <= 0)
                                syserr("msgrcv in server");

        }

        /* send final data  */
        mesg.mesg_data[0] = 0;
        if (msgsnd(send_qid, (char *) &mesg, sizeof(mesg.mesg_data), 0) != 0)
            syserr("msgsnd in komisja");
        /* wait for confirmation with data summary  */
        if ((l = msgrcv(comout_qid, &mesg, sizeof(mesg.mesg_data), nr_komisji, 0)) <= 0)
                            syserr("msgrcv in server");
        /* check for correctness */
        /* write summary to stdout */
    };

    /* free resources */

    return 0;
}
