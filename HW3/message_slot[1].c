/*recitation 6*/
#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE
#include "message_slot.h"
/*recitation 6*/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
/*for errors*/
#include <linux/errno.h>
/*for kmalloc and kfree, taken from - https://stackoverflow.com/questions/13656913/compile-error-kernel-module*/
#include <linux/slab.h>

/*per http://derekmolloy.ie/writing-a-linux-kernel-module-part-2-a-character-device*/
#include <linux/init.h>
#include <linux/device.h>

/*General explanation - the implementation is of a message slot char device, with at most 256 minors and every minor has several channels is can 
communicate in. 
My implementation consists of 3 objects - 
1) msg_channel - represents a channel in the minor.
2) channels_list - a linked list of all channels allocated by ioctl.
3) msg_slot - a minor.
Every minor has and minor num and a pointer to the list of allocated channels in that particular minor num, there is an array of minors
because in ioctl we need to search for the desired channel in the minor (search in the list).
The space complexity of the whole message slot is the number of channels in the linked lists, we have C toatal node accross all
lists since for every channel allocated we have a list node, for each channel we have a dynamically allocated buffer (in the struct) with max size M so
and an array of constant size (235) of minors - O(M*C) as demended.
*/

/*recitataion 6*/
MODULE_LICENSE("GPL");

void free_list(int minor_num);
void free_lists(void);
static int device_init(void);
static int device_open(struct inode* indoe, struct file* fd);
static long int device_ioctl(struct file* fd, unsigned int command, unsigned long id);
static ssize_t device_read(struct file* fd, char* buffer, size_t buffer_size, loff_t* offset);
static ssize_t device_write(struct file* fd, const char* buffer, size_t buffer_size, loff_t* offset);
static void device_cleanup(void);


#define NAME "my_device"
/*from home recitation 6*/
struct file_operations fops = {
    .owner = THIS_MODULE,
	.read = device_read,
	.write = device_write,
	.open = device_open,
    .unlocked_ioctl = device_ioctl
};
/*A single message channel which is a node in the channels list of a minor. 
Buffer used to conatin the message and size is the buffer's size*/
struct msg_channel{
    int minor;
    char *buffer;
    int size;
    int id;
    struct msg_channel* next;
}x;
/*List of opened channels*/
struct channels_list
{
    struct msg_channel* head;
    struct msg_channel* tail;
}y;
/*A minor device, has it's channels list, minor number and current channel*/
struct msg_slot{
    int minor;
    struct channels_list* slot_channel_list;
    unsigned long id;
}z;
/*Array of minors*/
struct msg_slot* slots [MAX_MINORS];

static struct msg_channel msg_channel;
static struct channels_list channels_list;
static struct msg_slot msg_slot;

/*Search for channel id in the linked list that fits minor number*/
struct msg_channel* search_channel(unsigned int channel_id, int minor_num)
{
    struct msg_channel* p; 
    /*No channels in that minors*/
    if(slots[minor_num]->slot_channel_list == NULL)
    {
        slots[minor_num]->slot_channel_list = kmalloc(sizeof(struct channels_list), GFP_KERNEL);
        return NULL;   
    }
    /*First channel in the device list*/
    p = slots[minor_num]->slot_channel_list->head;
    while(p != NULL)
    {
        if(p ->id == channel_id)
            return p;
        p = p->next;
    }
    return NULL;
}
/*Free one list*/
void free_list(int minor_num)
{
    struct msg_channel* p; 
    if(slots[minor_num] == NULL)
    {
        return;
    }
    p = slots[minor_num]->slot_channel_list->head;
    while(p != NULL)
    {
        kfree(p);
        p = p->next;
    }
}
/*Free all device files lists*/
void free_lists()
{
    int i;
    for(i = 0; i < MAX_MINORS; i++)
    {
        free_list(i);
    }
}
/*Init function recitation 6*/
static int device_init(void)
{
    int err;
    memset(&msg_channel, 0, sizeof(struct msg_channel));
    memset(&channels_list, 0, sizeof(struct channels_list));
    memset(&msg_slot, 0, sizeof(struct msg_slot));
    err = register_chrdev(MY_MAJOR, NAME, &fops);
    if(err != 0)
    {
        printk(KERN_ERR);
        return err;
    }
    return 0;
}
static int device_open(struct inode* inode, struct file* fd)
{
    struct msg_slot* slot;
    int num;
    if(fd == NULL)
    {
        return -1;
    }
    slot = kmalloc(sizeof(struct msg_slot), GFP_KERNEL);
    if(slot == NULL)
    {
        return -1;
    }
    /*Get the minor number, iminor from the assignment description*/
    slot -> minor = iminor(inode);
    /*fd uninitilized channel*/
    slot ->id = 0;
    num = slot->minor;
    /*Set the file to the device file*/
    fd->private_data = (void*) slot;
    /*If this is a new minor allocate a new array minor*/
    if(slots[num] == NULL)
    {
        slots[num] = slot;
    }
    return 0;
}

static long device_ioctl(struct file* fd,unsigned int command, unsigned long id)
{
    struct msg_channel* channel;
    int minor_num;
    /*Error cases*/
    if(command != MSG_SLOT_CHANNEL || id == 0) 
    {
        printk("Invalid argument");
        return -EINVAL;
    }
    /*https://stackoverflow.com/questions/12982318/linux-device-driver-is-it-possible-to-get-the-minor-number-using-a-file-descrip*/
    minor_num = iminor(fd->f_path.dentry->d_inode);
    /*Search a channel with id same as parameter */
    channel = (struct msg_channel*) search_channel(id, minor_num);
    if((void*)channel == NULL)
    {
        /*In case no channel with that id found, allocate a new one*/
        channel = kmalloc(sizeof(struct msg_channel), GFP_KERNEL);
        channel -> minor = minor_num;
        channel -> size = 0;
        channel -> id = id;
        channel -> next = NULL;
        channel -> buffer = kmalloc(128, GFP_KERNEL);
        /*Add it to the list fits to that minor*/
        if(slots[minor_num]->slot_channel_list->head == NULL || slots[minor_num]->slot_channel_list->tail == NULL)
        {
            /*No channel allocated in that minor*/
            slots[minor_num]->slot_channel_list-> head = kmalloc(sizeof(struct msg_channel), GFP_KERNEL);
            slots[minor_num]->slot_channel_list->head = channel;
            slots[minor_num]->slot_channel_list->tail = kmalloc(sizeof(struct msg_channel), GFP_KERNEL);
            slots[minor_num]->slot_channel_list->tail = channel;
        }
        else
        {
            /*Add that channel to the tail of the list*/
            slots[minor_num]->slot_channel_list->tail->next = kmalloc(sizeof(struct msg_channel), GFP_KERNEL);
            slots[minor_num]->slot_channel_list->tail->next = channel;
            slots[minor_num]->slot_channel_list->tail = slots[minor_num]->slot_channel_list->tail->next;
        }
    }
    slots[minor_num]->id = id;
    slots[minor_num]->minor = minor_num;
    /*Associate slot with fd*/
    fd->private_data = (void*) slots[minor_num];
    return 0;
}

static ssize_t device_read(struct file* fd, char* buffer, size_t buffer_size, loff_t* offset)
{
    struct msg_channel *channel_to_read; 
    struct msg_slot* slot;
    int i;
    /*Recived NULL fd*/
    if(fd == NULL)
    {
        printk("Invalid argument");
        return -EINVAL;
    }
     slot = (struct msg_slot*) fd->private_data;
     if(slot == NULL)
     {
         printk("Invalid argument");
        return -EINVAL;
     }
    /*Passed channel id is 0*/
    if(slot->id == 0)
    {
        printk("Invalid argument");
        return -EINVAL;
    }
    /*Get the channel struct msg_channel from fd*/
    channel_to_read = (struct msg_channel*)search_channel(slot->id, slot->minor);
    if(channel_to_read == NULL)
    {
        printk("Invalid argument");
        return -EINVAL;
    }
    /*Error cases*/
    if(buffer == NULL)
    {
        printk("Invalid argument");
        return -EINVAL;
    }
    /*Indicates buffer size 0*/
    if(channel_to_read->size == 0)
    {
        printk("Operation would block");
        return -EWOULDBLOCK;
    }
    /*Message to long*/
    if(buffer_size < channel_to_read->size)
    {
        printk("No space left on device");
        return -ENOSPC;
    }
    /*Read at most the size of the message, even if buffer is bigger*/
    buffer_size = min((int)buffer_size, channel_to_read->size);
    for (i = 0; i < buffer_size; i++)
    {
        /*recitation 6*/
        if(put_user(*(channel_to_read->buffer + i), buffer+i) != 0)
        {
            printk("Error");
            return -1;
        }
    }
   return buffer_size;
}

static ssize_t device_write(struct file* fd, const char* buffer, size_t buffer_size, loff_t* offset)
{
    struct msg_channel *channel_to_write;
    struct msg_slot* slot;
    int i;

    if(fd == NULL)
    {
        printk("Invalid argument");
        return -EINVAL;
    }
    slot = (struct msg_slot*) (fd->private_data);
    if(slot == NULL)
    {
        printk("Resource tamporarly unavailable");
        return -EINVAL;
    }
    /*Passed channel id is 0*/
    if(slot->id == 0)
    {
        printk("Invalid argument");
        return -EINVAL;
    }
    channel_to_write = (struct msg_channel*)search_channel(slot->id, slot->minor);
    if(channel_to_write == NULL)
    {
        printk("Invalid argument");
        return -EINVAL;
    }
    /*Size of message to write is to long*/
    if(buffer_size > 128 || buffer_size == 0)
    {
        printk("Message too long");
        return -EMSGSIZE;
    }
    /*Overwrite the message currently in the buffer*/
    kfree(channel_to_write->buffer);
    channel_to_write->buffer = kmalloc(buffer_size, GFP_KERNEL);
    channel_to_write->size = buffer_size;
    for (i = 0; i < buffer_size; i++)
    {
        /*recitation 6*/
        if(get_user(*(channel_to_write->buffer+i), buffer+i) < 0)
        {
            printk("Error");
            return -1;
        }
    }
    return buffer_size;
}
/*Cleanup recitation 6*/
static void device_cleanup(void)
{
    free_lists();
    unregister_chrdev(MY_MAJOR, NAME);
}
/*recitation 6*/
module_init(device_init);
module_exit(device_cleanup);