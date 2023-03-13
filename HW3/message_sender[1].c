#include "message_slot.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>


void send(char* file_path, char* msg, int id)
{
    int fd, i, len=0;
    //size_t size;
    fd = open(file_path,O_RDWR);
    if ( fd == -1) {
      perror ("Error");
      exit(1);
    }
    ioctl(fd, MSG_SLOT_CHANNEL, id);
    for(i = 0; *(msg+i) != '\0'; i++)
    {
        len++;
    }
    if(write(fd, msg, len) == -1)
    {
        perror("Error");
        exit(1);
    }
    close(fd); 
    exit(0); 
}
int main(int argc, char* argv[])
{
    char* file_path, *msg;
    int id;
    if(argc != 4)
    {
        return -1;
    }
    file_path = argv[1];
    /*Change to int*/
    id = atoi(argv[2]);
    msg = argv[3];
    send(file_path, msg, id);
    return 0; 
}