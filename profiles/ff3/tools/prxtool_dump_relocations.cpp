// SPDX-License-Identifier: MIT
// Build against external PRXTool; never link it into the runtime.
#include "ProcessPrx.h"
#include <cstdio>
int main(int argc,char **argv) {
    if(argc!=2)return 1;
    CProcessPrx module(0);
    if(!module.LoadFromFile(argv[1]))return 2;
    int count=0;auto rels=module.GetRelocs(count);
    std::printf("type,symbol,offset,info,addend\n");
    for(int i=0;i<count;++i)std::printf("%u,%u,%u,%u,%u\n",rels[i].type,rels[i].symbol,rels[i].offset,rels[i].info,rels[i].base);
}
