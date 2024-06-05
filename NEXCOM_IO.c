#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 0)
#include <linux/cdev.h>
#include <linux/delay.h>
static struct cdev mycdev;
static dev_t devno=0;
#endif

/* for 2.6.36 and after unlocked_ioctl */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36)
#include <linux/mutex.h>
static DEFINE_MUTEX(device_mutex);
#endif
static struct class *charmodule_class;
static int tg_major = 0;


#define BASEADDR 0x0ee5		// Base address
#define ADDRRLEN 1		// Address range length
#define DEVNAME "NEXCOM_IO"
#define DEBUG 0

static unsigned char *prbuf, *pwbuf;

static int NEXCOM_IO_open (struct inode *inode, struct file *filp) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 0)
	try_module_get(THIS_MODULE);
#else
	MOD_INC_USE_COUNT;
#endif
//	printk(KERN_DEBUG DEVNAME"_open()\n");

	return 0;
}


static int NEXCOM_IO_close(struct inode *inode, struct file *file) {
//	printk(KERN_DEBUG DEVNAME"_close()\n");
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 0)
	module_put(THIS_MODULE);
#elif LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 0)
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}


static ssize_t NEXCOM_IO_read (struct file *filp, char *buf, size_t count, loff_t *t) {
	char value;

	value = inb(count);
#if DEBUG
	printk(KERN_DEBUG DEVNAME"_read %x=%x\n", count, value);
#endif
	put_user(value, buf);
   
	return 0;
}


static ssize_t NEXCOM_IO_write (struct file *filp, const char *buf, size_t count, loff_t *t) {
	unsigned int addr;
	unsigned char data;

	/* Get Address */
	addr = count;

	/* Get Data */
	get_user(data, buf);

#if DEBUG
	printk(KERN_DEBUG DEVNAME"_write %x=%x\n", addr, data);
#endif

	outb(data, addr);

	return 0;
}



struct file_operations tg_fops = {
	.owner = THIS_MODULE,
	.read = NEXCOM_IO_read,
	.write = NEXCOM_IO_write,
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36)
	.unlocked_ioctl = NULL, /* ioctl */
#else
	.ioctl = NULL, /* ioctl */
#endif
	.open = NEXCOM_IO_open,
	.release = NEXCOM_IO_close
};


int __init init_module(void)
{ 
	printk(KERN_DEBUG DEVNAME": Init module\n");
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,9)
	
#elif LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 0)
	if (!request_region(BASEADDR, ADDRRLEN, DEVNAME)) {
		printk(KERN_ERR DEVNAME": Can't get I/O address "BASEADDR"\n");
		return -ENODEV;
	}
#else
	if (check_region(BASEADDR, ADDRRLEN)) {
		printk(KERN_ERR DEVNAME": Can't get I/O address 0x%x\n", BASEADDR);
		return -ENODEV;
	}
	request_region(BASEADDR, ADDRRLEN, DEVNAME);
#endif

	// register character driver
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 0)
	if(tg_major) {
		if (register_chrdev_region(MKDEV(tg_major, 0), 1, DEVNAME) < 0 ) {
			printk (KERN_ERR DEVNAME": register_chrdev_region() fail\n");
			release_region(BASEADDR, ADDRRLEN);
			return -ENOMEM;
		}
	}
	else {
		if (alloc_chrdev_region(&devno, 0, 1, DEVNAME) < 0) {
			printk (KERN_ERR DEVNAME": alloc_chrdev_region() fail\n");
			release_region(BASEADDR, ADDRRLEN);
			return -ENOMEM;
		}
		tg_major = MAJOR(devno);
	}
	cdev_init(&mycdev, &tg_fops);
	mycdev.owner = THIS_MODULE;
	if(cdev_add(&mycdev, MKDEV(tg_major, 0), 1)) {
		printk (KERN_ERR DEVNAME": Error adding cdev\n");
		unregister_chrdev_region(MKDEV(tg_major, 0), 1);
		release_region(BASEADDR, ADDRRLEN);
	}
#else
	tg_major = register_chrdev(0, DEVNAME, &tg_fops);
	if (tg_major < 0) {
	printk(KERN_ERR DEVNAME": Can't get major number\n");
		release_region(BASEADDR, ADDRRLEN);
		return -EINVAL;
	}
#endif
	charmodule_class = class_create(THIS_MODULE, DEVNAME);
	// allocate read/write buffer
	prbuf = (unsigned char *)__get_free_page(GFP_KERNEL);
	pwbuf = (unsigned char *)__get_free_page(GFP_KERNEL);
	if (!prbuf || !pwbuf) {
		release_region(BASEADDR, ADDRRLEN);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 0)
		cdev_del(&mycdev);
		unregister_chrdev_region(MKDEV(tg_major, 0), 1);
#else
		unregister_chrdev(tg_major, DEVNAME);
#endif
		return -ENOMEM;
	}
   
   
	device_create(charmodule_class, NULL, MKDEV(tg_major, 0), NULL, DEVNAME);
	printk(KERN_DEBUG DEVNAME": Module initialized \n");	
	return 0;
}


void __exit cleanup_module(void)
{ 
	printk(KERN_DEBUG DEVNAME": Cleanup module\n");	
	device_destroy(charmodule_class, MKDEV(tg_major, 0));
	
	// To do: release read/write buffer
	if( prbuf) free_page((unsigned long)prbuf);
	if( pwbuf) free_page((unsigned long)pwbuf);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 0)
	cdev_del(&mycdev);
	unregister_chrdev_region(MKDEV(tg_major, 0), 1);
#else
	unregister_chrdev(tg_major, DEVNAME);
#endif
	class_destroy(charmodule_class);
	release_region(BASEADDR, ADDRRLEN);
	printk(KERN_DEBUG DEVNAME": Module cleared \n");	
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IO driver for the NEXCOM platform");
