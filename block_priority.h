#ifndef BLOCK_PRIORITY_H
#define BLOCK_PRIORITY_H
#include <stdint.h>
#include <time.h>
#define MAX_STAT 4096
typedef struct{uint64_t off;time_t t;int cnt;} Stat;
static Stat stats[MAX_STAT];
static void update_stat(uint64_t o){
    for(int i=0;i<MAX_STAT;i++){
        if(stats[i].off==o||stats[i].off==0){
            stats[i].off=o; stats[i].t=time(NULL); stats[i].cnt++;break;
        }
    }
}
static int is_hot(uint64_t o){
    for(int i=0;i<MAX_STAT;i++){
        if(stats[i].off==o){
            return stats[i].cnt>2 && (time(NULL)-stats[i].t<5);
        }
    }
    return 0;
}
#endif
