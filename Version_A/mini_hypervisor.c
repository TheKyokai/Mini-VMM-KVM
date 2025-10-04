#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int parse_arguments(int argc, char *argv[], int *memory, int *page, char **guest_image){
    if (argc < 7){
        printf("Error: incorrect command format\nCorrect format: ./mini_hypervisor <--memory/-m> <2/4/8(MB)> <--page/-p> <4(KB)/2(MB)> <--guest/-g> <guest image file>\nAll arguments are mandatory\n");
        return -1;
    }
    int mem_done = 0;
    int page_done = 0;
    int guest_done = 0;
    for (int i=1; i<6; i+=2){
        if ((!strcmp("--memory", argv[i]) || !strcmp("-m", argv[i])) && !mem_done){
            int mem_size = atoi(argv[i+1]);
            if (mem_size == 2 || mem_size == 4 || mem_size == 8){
                *memory = mem_size;
                mem_done = 1;
                continue;
            } else {
                printf("Error: guest physical memory size must be 2, 4 or 8(MB)\nRequested: %s\n", argv[i+1]);
                return -2;
            }
        }
        if ((!strcmp("--page", argv[i]) || !strcmp("-p", argv[i])) && !page_done){
            int page_size =  atoi(argv[i+1]);
            if (page_size == 2 || page_size == 4){
                *page = page_size;
                page_done = 1;
                continue;
            } else {
                printf("Error: page size must be 4(KB) or 2(MB)\nRequested: %s\n", argv[i+1]);
                return -3;
            }
        }
        if ((!strcmp("--guest", argv[i]) || !strcmp("-g", argv[i])) && !guest_done){
            *guest_image =  argv[i+1];
            guest_done = 1;
        }
    }
    if (guest_done && mem_done && page_done){
        return 0;
    }
    printf("Error: Not all provided arguments are valid\n");
    if (!mem_done){
        printf("Guest physical memory not specified\nUse: --memory/-m 2/4/8(MB)\n");
    }
    if (!page_done){
        printf("Page size not specified\nUse: --page/-p 4(KB)/2(MB)\n");
    }
    if (!guest_done){
        printf("Guest image file not specified\nUse: --guest/-g <guest image file>\n");
    }
    return -4;
}


int main(int argc, char *argv[])
{
    int memory, page, arg_status;
    char *guest_image = NULL;
    if ((arg_status = parse_arguments(argc, argv, &memory, &page, &guest_image)) < 0){
        return arg_status;
    }
    

    printf("Guest PM: %d\n", memory);
    printf("Page Size: %d\n", page);
    printf("Guest IMG: %s\n", guest_image);
    


    return 0;
}
