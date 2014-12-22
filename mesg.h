#define MAXMESGDATA 4

typedef struct {
    long mesg_type;
    /* this is used as id of client equivalent to "address"
     * where the message should go. */
    int data[MAXMESGDATA];
    /* first integer is "message"
     * three next are "data" 
     * mesg_data[0] meanings:
     * in MAIN_IN_Q:
     *     1 - request handler
     * in COMMITTEE_OUT_Q:
     *     0 - handler not granted (committee has already reported)
     *     1 - handler granted
     * in COMMITTEE_IN_Q:
     *     1 - sending initial data
     *     2 - sending data about list
     *     3 - end 
     * */
} Mesg;

#define MAIN_IN_Q             1042L
#define REPORT_Q              1017L
#define COMMITTEE_Q_IN        2023L
#define COMMITTEE_OUT_Q       1905L
#define DEBUG                 1
