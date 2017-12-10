#include <linux/init.h>           
#include <linux/module.h>        
#include <linux/device.h>         
#include <linux/kernel.h>         
#include <linux/fs.h>             
#include <asm/uaccess.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/errno.h>    
#include <linux/delay.h>
#include <linux/types.h>  
#include <linux/kdev_t.h> 
#include <linux/ioport.h>
#include <linux/highmem.h>
#include <linux/pfn.h>
#include <linux/version.h>
#include <linux/ioctl.h>
#include <net/sock.h>
#include <net/tcp.h>	          
#include <linux/unistd.h>

#define  DEVICE_NAME "BBB_Driver"    
#define  CLASS_NAME  "BBB"

#define CQ_DEFAULT	0

#define GPIO1_START_ADDR 0x4804C000
#define GPIO1_END_ADDR   0x4804e000
#define GPIO1_SIZE (GPIO1_END_ADDR - GPIO1_START_ADDR)

#define GPIO_SETDATAOUT 0x194
#define GPIO_CLEARDATAOUT 0x190
#define USR3 (1<<24)
#define USR0 (1<<21)

#define USR_LED USR3
#define LED0_PATH "/sys/class/leds/beaglebone:green:usr0"

//macros for pauses
#define WORD_PAUSE 2000
#define CHAR_PAUSE 500
#define DOT_PAUSE 500
#define DASH_PAUSE 1500

MODULE_LICENSE("GPL");            
MODULE_AUTHOR("Dominick DeJesus");    
MODULE_DESCRIPTION("A device driver that gets passed a string and blinks the Beaglbone Black's LED in morse code"); 
MODULE_VERSION("0.1");            

static void BBBremoveTrigger(void);
static void BBBstartHeartbeat(void);
static void BBBledOn(void);
static void BBBledOff(void);

static int majorNumber;                  
static int numberOpened = 0;              
static struct class*  BBBClass  = NULL; 
static struct device* BBBDevice = NULL;
static DEFINE_MUTEX(BBB_mutex);

static int bdev_open(struct inode *inode, struct file *file);
static int     bdev_release(struct inode *, struct file *);
static ssize_t bdev_read(struct file *, char *, size_t, loff_t *);
static ssize_t bdev_write(struct file *, const char *, size_t, loff_t *);
static char* mcodestring(int );

static volatile void *gpio_addr;
static volatile unsigned int *gpio_setdataout_addr;
static volatile unsigned int *gpio_cleardataout_addr;


static ssize_t write_vaddr_disk(void *, size_t);
static int setup_disk(void);
static void cleanup_disk(void);
static void disable_dio(void);

static struct file * f = NULL;
static int reopen = 0;
static char *filepath = 0;
static char fullFileName[1024];
static int dio = 0;

static struct file_operations fops =
{
   .open = bdev_open,
   .read = bdev_read,
   .write = bdev_write,
   .release = bdev_release,
};
// This initialization function was taken from www.derekmolloy.ie and modified for the purpose of this assignment.
static int __init BBB_Driver_init(void)
{
   printk(KERN_INFO "BBB_Driver: Initializing the BBB_Driver DD\n");
   gpio_addr = ioremap(GPIO1_START_ADDR, GPIO1_SIZE);
   
   if(!gpio_addr) 
   {
     printk (KERN_ERR "HI: ERROR: Failed to remap memory for GPIO Bank 1.\n");
   }

   gpio_setdataout_addr   = gpio_addr + GPIO_SETDATAOUT;
   gpio_cleardataout_addr = gpio_addr + GPIO_CLEARDATAOUT;

   //BBBremoveTrigger();
   BBBledOn();
   msleep(1000);
   BBBledOff();
   msleep(1000);
   BBBledOn();

   majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
   if (majorNumber<0)
   {
      printk(KERN_ALERT "BBB_Driver: failed to register a major number\n");
      return majorNumber;
   }
   printk(KERN_INFO "BBB_Driver: registered correctly with major number %d\n", majorNumber);

   BBBClass = class_create(THIS_MODULE, CLASS_NAME);
   if (IS_ERR(BBBClass))
   {                
      unregister_chrdev(majorNumber, DEVICE_NAME);
      printk(KERN_ALERT "Failed to register device class\n");
      return PTR_ERR(BBBClass);         
   }
   printk(KERN_INFO "BBB_Driver: Device class registered correctly\n");

   BBBDevice = device_create(BBBClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
   if (IS_ERR(BBBDevice))
   {               
      class_destroy(BBBClass);           
      unregister_chrdev(majorNumber, DEVICE_NAME);
      printk(KERN_ALERT "Failed to create the device\n");
      return PTR_ERR(BBBDevice);
   }
   printk(KERN_INFO "BBB_Driver: Device class created correctly\n"); 
   return 0;
   mutex_init(&BBB_mutex);
}

static void __exit BBB_Driver_exit(void)
{
   mutex_destroy(&BBB_mutex);    
   device_destroy(BBBClass, MKDEV(majorNumber, 0));     
   class_unregister(BBBClass);                          
   class_destroy(BBBClass);                             
   unregister_chrdev(majorNumber, DEVICE_NAME); 
   BBBledOff();
   //BBBstartHeartbeat();         
   printk(KERN_INFO "BBB_Driver: Device has been cleaned and removed!\n");
}

static int bdev_open(struct inode *inode, struct file *file)
{
   mutex_lock_interruptible(&BBB_mutex);
   numberOpened++;
   printk(KERN_INFO "BBB_Driver: Device has been opened %d time(s) by user(s).", numberOpened);
   return 0;
}

static ssize_t bdev_write(struct file *filep, const char *buffer, size_t length, loff_t *offset)
{
    printk(KERN_INFO "BBB_Driver: Received %zu characters from the user.\n", length);

    if(buffer == NULL)
    {
    	printk("BBB_Driver: Error! No input!");
    }
    else
	{
	        printk(KERN_INFO "BBB_Driver: Started sending MCode message\n");
                char* morseChar = NULL;
		int indI=0;
		while(buffer[indI] != '\0')		//until end of string
		{
			int asciiInt = (int) buffer[indI];
			morseChar = mcodestring(asciiInt);
			int indM = 0;
                        printk(KERN_INFO "BBB_Driver:  MCode message %c\n", morseChar[indM]);
			// loops through the dot dashes for one char
			while(morseChar[indM] != '\0')
			{
				if (morseChar[indM] == ' ')
				{
					msleep(WORD_PAUSE);
				}
				else if(morseChar[indM] == '.')
				{
					BBBledOn();
					msleep(DOT_PAUSE);
					BBBledOff();
				}
				else if(morseChar[indM] == '-')
				{
					BBBledOn();
					msleep(DASH_PAUSE);
					BBBledOff();
				}

				msleep(CHAR_PAUSE);
				indM++;
			}
			indI++;
		}
   }
   BBBledOn(); 
   return 0;
}

static ssize_t bdev_read(struct file *filep, char *buffer, size_t length, loff_t *offset)
{
   printk(KERN_INFO "BBB_Driver: Read from DD.\n");
   return 0;
}

static int bdev_release(struct inode *inodep, struct file *filep)
{
   mutex_unlock(&BBB_mutex);
   numberOpened--;
   printk(KERN_INFO "BBB_Driver: Device successfully closed by user.\n");
   return 0;
}

/* the empty string, follwed by 26 letter codes, followed by the 10 numeral codes, followed by the comma,
   period, and question mark.  */

static char * morse_code[40] = {"",
".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",
".-..","--","-.","---",".--.","--.-",".-.","...","-","..-","...-",
".--","-..-","-.--","--..","-----",".----","..---","...--","....-",
".....","-....","--...","---..","----.","--..--","-.-.-.","..--.."};


static char * mcodestring(int asciicode)
{
   char *mc;   // this is the mapping from the ASCII code into the mcodearray of strings.

   if (asciicode > 122)  // Past 'z'
      mc = morse_code[CQ_DEFAULT];
   else if (asciicode > 96)  // Upper Case
      mc = morse_code[asciicode - 96];
   else if (asciicode > 90)  // uncoded punctuation
      mc = morse_code[CQ_DEFAULT];
   else if (asciicode > 64)  // Lower Case
      mc = morse_code[asciicode - 64];
   else if (asciicode == 63)  // Question Mark
      mc = morse_code[39];    // 36 + 3
   else if (asciicode > 57)  // uncoded punctuation
      mc = morse_code[CQ_DEFAULT];
   else if (asciicode > 47)  // Numeral
      mc = morse_code[asciicode - 21];  // 27 + (asciicode - 48)
   else if (asciicode == 46)  // Period
      mc = morse_code[38];  // 36 + 2
   else if (asciicode == 44)  // Comma
      mc = morse_code[37];   // 36 + 1
   else
      mc = morse_code[CQ_DEFAULT];
   return mc;
}

/*
static void BBBremoveTrigger(){
   // remove the trigger from the LED
   int err = 0;
  
  strcpy(fullFileName, LED0_PATH);
  strcat(fullFileName, "/");
  strcat(fullFileName, "trigger");
  printk(KERN_INFO "File to Open: %s\n", fullFileName);
  filepath = fullFileName; // set for disk write code
  err = setup_disk();
  err = write_vaddr_disk("none", 4);
  cleanup_disk();
}

static void BBBstartHeartbeat(){
   // start heartbeat from the LED
     int err = 0;
  

  strcpy(fullFileName, LED0_PATH);
  strcat(fullFileName, "/");
  strcat(fullFileName, "trigger");
  printk(KERN_INFO "File to Open: %s\n", fullFileName);
  filepath = fullFileName; // set for disk write code
  err = setup_disk();
  err = write_vaddr_disk("heartbeat", 9);
  cleanup_disk();
}
*/
static void BBBledOn()
{
   printk(KERN_INFO "LEDON\n");
   *gpio_setdataout_addr = USR_LED;
}


static void BBBledOff()
{
   *gpio_cleardataout_addr = USR_LED;
}

/*
static void disable_dio() {
   dio = 0;
   reopen = 1;
   cleanup_disk();
   setup_disk();
}

static int setup_disk() {
   mm_segment_t fs;
   int err;

   fs = get_fs();
   set_fs(KERNEL_DS);
	
   if (dio && reopen) {	
      f = filp_open(filepath, O_WRONLY | O_CREAT | O_LARGEFILE | O_SYNC | O_DIRECT, 0444);
   } else if (dio) {
      f = filp_open(filepath, O_WRONLY | O_CREAT | O_LARGEFILE | O_TRUNC | O_SYNC | O_DIRECT, 0444);
   }
	
   if(!dio || (f == ERR_PTR(-EINVAL))) {
      f = filp_open(filepath, O_WRONLY | O_CREAT | O_LARGEFILE | O_TRUNC, 0444);
      dio = 0;
   }
   if (!f || IS_ERR(f)) {
      set_fs(fs);
      err = (f) ? PTR_ERR(f) : -EIO;
      f = NULL;
      return err;
   }

   set_fs(fs);
   return 0;
}

static void cleanup_disk() {
   mm_segment_t fs;

   fs = get_fs();
   set_fs(KERNEL_DS);
   if(f) filp_close(f, NULL);
   set_fs(fs);
}

static ssize_t write_vaddr_disk(void * v, size_t is) {
   mm_segment_t fs;

   ssize_t s;
   loff_t pos;

   fs = get_fs();
   set_fs(KERNEL_DS);
	
   pos = f->f_pos;
   s = vfs_write(f, v, is, &pos);
   if (s == is) {
      f->f_pos = pos;
   }					
   set_fs(fs);
   if (s != is && dio) {
      disable_dio();
      f->f_pos = pos;
      return write_vaddr_disk(v, is);
   }
   return s;
}
*/
module_init(BBB_Driver_init);
module_exit(BBB_Driver_exit);

