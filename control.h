#include <pthread.h>
typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t cnd;
    int val;
} DataAccessControl;

