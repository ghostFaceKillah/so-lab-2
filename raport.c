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
    int msg_qid, rep_qid;
    int l;
    int y, z, v, n, M;
    int com_no;
    if (argc == 2) {
        com_no = atoi(argv[1]);
    } else if (argc == 1) {
        com_no = 0;
    } else {
        syserr("raport: wrong number of arguments specified");
    }
    printf("got param %d from cmnd line\n", com_no);

    /* get main queue id */
    if ((msg_qid = msgget(MAIN_IN_Q, 0)) == -1)
         syserr("msgget in komisja main queue");
    if ((rep_qid = msgget(REPORT_Q, 0)) == -1)
         syserr("msgget in komisja committee out q");

    /* requesting handler creation from server by sending our commision number
     * by message type */
    mesg.mesg_type = 1;  /* 1 means report sends to server. 2 or more means reverse */
    mesg.data[0] = 2;    /* means we are requesting report handler */
    if (DEBUG)
        printf("requesting handler from server \n");
    if (msgsnd(msg_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
        syserr("msgsnd in report");

    /* receive confirmation */
    if ((l = msgrcv(rep_qid, &mesg, sizeof(mesg.data), 2, 0)) <= 0)
        syserr("msgrcv in report");
    assert(mesg.data[0] == 1);

    /* receive no of processed committees data */
    if ((l = msgrcv(rep_qid, &mesg, sizeof(mesg.data), 3, 0)) <= 0)
        syserr("msgrcv in report");
    n = mesg.data[0];
    M = mesg.data[1];
    printf("Przetworzonych komisji %d / %d\n", n, M);

    /* receive election turnout data */
    if ((l = msgrcv(rep_qid, &mesg, sizeof(mesg.data), 4, 0)) <= 0)
        syserr("msgrcv in report");
    y = mesg.data[0];
    z = mesg.data[1];
    v = mesg.data[2];
    printf("Uprawnionych do głosowania: %d\n", y);
    printf("Głosów ważnych: %d\n", z);
    printf("Głosów nieważnych: %d\n", v);
    float turnout = (float) z + (float) v;
    turnout /= (float) y;
    turnout *= 100;
    printf("Frekwencja: %f%%\n", turnout);
    printf("Wyniki poszczególnych list: \n");

    /* send type of report needed */
    mesg.mesg_type = 7;
    mesg.data[0] = com_no;
    if (msgsnd(rep_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
        syserr("msgsnd in report");

    int get_next_list = 1;
    while (get_next_list) {
        /* get list_no and sum of votes for candidates from this list */
        if ((l = msgrcv(rep_qid, &mesg, sizeof(mesg.data), 5, 0)) <= 0)
            syserr("msgrcv in report");
        printf("%d %d", mesg.data[0], mesg.data[1]);
        /* send confirmation */
        mesg.mesg_type = 1;
        if (msgsnd(rep_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
            syserr("msgsnd in report");
        /* get votes for all of the candidates */
        int cont = 1;
        while (cont) {
            if ((l = msgrcv(rep_qid, &mesg, sizeof(mesg.data), 6, 0)) <= 0)
                syserr("msgrcv in report");
            if (mesg.data[0] == 13) {
                /* got end of list signal */
                cont = 0;
                /* send confirmation */
                mesg.mesg_type = 1;
                if (msgsnd(rep_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
                    syserr("msgsnd in report");
            } else {
                /* got another candidate's no of votes */
                printf(" %d", mesg.data[1]);
                /* send confirmation */
                mesg.mesg_type = 1;
                if (msgsnd(rep_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
                    syserr("msgsnd in report");
            }
        }
        printf("\n");
        /* one more list ? - get message */
        if ((l = msgrcv(rep_qid, &mesg, sizeof(mesg.data), 6, 0)) <= 0)
            syserr("msgrcv in report");
        /* send confirmation */
        mesg.mesg_type = 1;
        if (msgsnd(rep_qid, (char *) &mesg, sizeof(mesg.data), 0) != 0)
            syserr("msgsnd in report");
        /* one more list ? */
        if (mesg.data[0] == 17) {
            get_next_list = 0;
        } else if (mesg.data[0] == 14) {
            get_next_list = 1;
        } else {
            syserr("raport: got incorrect message number from handler.");
        }
    };

    /* free stuff ?? */

    return 0;
}
