#ifndef SMART_CACHE_H
#define SMART_CACHE_H
#include <stdint.h>
#include <string.h>
#include <time.h>
#define CSZ 128
#define BSZ 4096
typedef struct{char data[BSZ];uint64_t id;float sc;time_t lh;int v;} CE;
typedef struct{CE e[CSZ];} SmartCache;
static float ema(float p,float a,int h){return a*h+(1-a)*p;}
static void sc_init(SmartCache*c){memset(c,0,sizeof(*c));}
static CE* sc_lookup(SmartCache*c,uint64_t id){
    for(int i=0;i<CSZ;i++){
        if(c->e[i].v&&c->e[i].id==id){
            c->e[i].sc=ema(c->e[i].sc,0.6,1);
            c->e[i].lh=time(NULL);
            return &c->e[i];
        } else c->e[i].sc=ema(c->e[i].sc,0.6,0);
    }
    return NULL;
}
static void sc_insert(SmartCache*c,uint64_t id,char*d){
    int mi=-1;float ms=1e9;
    for(int i=0;i<CSZ;i++){
        if(!c->e[i].v){mi=i;break;}
        if(c->e[i].sc<ms){ms=c->e[i].sc;mi=i;}
    }
    if(mi>=0){
        c->e[mi].v=1; c->e[mi].id=id;
        c->e[mi].sc=0.5; c->e[mi].lh=time(NULL);
        memcpy(c->e[mi].data,d,BSZ);
    }
}
#endif
