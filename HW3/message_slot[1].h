#include <linux/ioctl.h>

#define MAX_MINORS 256
#define MY_MAJOR 235
#define BUF_LEN 128
#define MSG_SLOT_CHANNEL _IOW(MY_MAJOR,0,unsigned long)/*recitation 6*/
#define READ 0
#define WRITE 1
#define BUFFER_LEN 128

