#ifndef ANTICIPATOR_H
#define ANTICIPATOR_H
#include <stdint.h>
#include <time.h>
#define PRED 512
typedef struct{uint64_t off;int conf;time_t t;} PE;
static PE table[PRED];
static void learn(uint64_t o){
    for(int i=0;i<PRED;i++){
        if(table[i].off==o||table[i].off==0){
            table[i].off=o; table[i].conf++; table[i].t=time(NULL); return;
        }
    }
}
static int prefetch_ok(uint64_t o){
    for(int i=0;i<PRED;i++){
        if(table[i].off==o && table[i].conf>3 && (time(NULL)-table[i].t<10)) return 1;
    }
    return 0;
}
#endif
