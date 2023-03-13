#include "message_slot.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

/*Reader function*/
void m_read(char* file_path, int id)
{
    int fd; 
    size_t size, sizeW;
    char buffer[128];
    fd = open(file_path, O_RDWR);
    if(fd == -1)
    {
        perror ("Error");
        exit(1); 
    }
    ioctl(fd, MSG_SLOT_CHANNEL, id);
    size = read(fd, buffer, 128); 
    if(size == -1)
    {
      perror ("Error");
      exit(1);
    }
    close(fd);
    /*Write to stdoutput*/
    buffer[size] = '\0';
    sizeW = write(WRITE, buffer, size);
    if(sizeW == -1)
    {
      perror ("Error");
      exit(1);
    }
    exit(0);
}
int main(int argc, char* argv[])
{
    char* file_path;
    int id;
    if(argc != 3)
    {
        return -1;
    }
    file_path = argv[1];
    /*Change to int*/
    id = atoi(argv[2]);
    m_read(file_path, id);
    return 0; 
}