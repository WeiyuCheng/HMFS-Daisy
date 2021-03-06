#include <linux/module.h>//与module相关的信息
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/kdev_t.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include<linux/list.h>
#include<linux/slab.h>
#include<linux/mmzone.h>
#include<asm/page.h>
#include <linux/cdev.h>
#include<linux/mm_types.h>
#include<linux/mm.h>
#include<linux/highmem.h>
#include <linux/device.h>

#include<linux/genhd.h>  
#include<linux/blkdev.h>  
#include<linux/bio.h> 


#include "pmdev.h"
#include "pmvolume.h"

#define DEVICE_NAME "PMDev"

static long nvm_ctl(struct file *filp,unsigned int cmd,unsigned long arg);
static int my_open(struct inode *inode, struct file *file);
static int my_release(struct inode *inode, struct file *file);
//struct pm_device *createDevice(int *device_no,unsigned int *device_size,unsigned int device_num);
struct pm_volume *createVolume(int *device_no,int *device_start,unsigned int *device_size,unsigned int device_num,int volume_no,unsigned long start);
struct pm_device *findDevicePos(int device_no);
unsigned long add_map(unsigned long vol_pfn, struct pm_volume *_pmv);
unsigned long add_map_reverse(unsigned long dev_pfn, struct pm_volume *_pmv);
static ssize_t nvm_write(struct file *filp,const char __user *user,size_t t,loff_t *f);
static ssize_t nvm_read(struct file *filp,char __user *user,size_t t,loff_t *f);
static loff_t pm_lseek(struct file *file, loff_t offset, int orig);
int deleteVolume(int volume_no);
struct device_page_use* find_page(struct file *file,unsigned long len);
void pm_write(struct file *file,void *content,unsigned long len);
void pm_read(struct file *file,void *target,unsigned long len);
int pm_mmap(struct file *filp, struct vm_area_struct *vma);
struct pm_device *createDevice(unsigned int device_num);

//!new 3
static unsigned long volume_addr(int volume_no);


static dev_t dev0;
static dev_t dev;
static int major;
static int major0;
static dev_t dev0;
static dev_t dev;
static struct cdev *pm_cdev0;
static struct cdev *pm_cdev;
static struct class *fpga_class0;
static struct class *fpga_class;

static char devname[20] = "PMDevice";
static int mutex = 0;//互斥用
static char* devName = "myDevice";//设备名
static int device_nums=3000;
unsigned long zone_size = 1UL << 10;

/*保存需要写入或者读取的所有页的数据结构*/
struct device_page_use{
	struct page* page;  //页的起始地址  
	unsigned int size;   //所需要的字节数
	unsigned int offset;  //偏移的字节数
	unsigned long zone_pfn; //zone的pfn
};
unsigned long pm_device_pfn;  //设备的起始地址
struct device_page_use*  device_pages=NULL; //保存需要的页的信息的数组的起始位置

struct ramdisk_private_data {
	int volume_no;
};

struct pm_ramdisk_blk {
	struct list_head list;

	int volume_no;
	struct gendisk *p_disk; //用来指向申请的gendisk结构体  
	struct request_queue *p_queue; //用来指向请求队列
};

struct major_list{
	struct list_head list;
	int major;
	int minor;
	int device_no;
};
struct major_list *major_head;

struct start_list{
	void *start;
	int device_no;
	struct list_head list;

};
struct start_list *start_head;

struct pm_volume *pmv;
struct pm_volume *pm_volume_arrays;
struct pm_device *pm_device_arrays;
struct pm_ramdisk_blk *pm_ramdisk_blk_list;


struct file_operations pStruct =
{ 
	open:my_open,
	llseek:pm_lseek, 
	release:my_release, 
	read:nvm_read, 
	mmap:pm_mmap,
	write:nvm_write,
	unlocked_ioctl:nvm_ctl 
};

//!new 1
//get device's address(after add_map)
static unsigned long volume_addr(int volume_no)
{
	struct list_head *p;
	struct pm_volume *pv;
	unsigned long addr;
      list_for_each(p, &pm_volume_arrays->list){
            pv = list_entry(p, struct pm_volume, list);
		if(pv!=NULL){
			if(pv->volume_no=volume_no)
			{
				addr=pv->start;
				break;
			}
			//printk("**vloume_no:%d**\n",pv->volume_no);
			//printk("**vloume_addr:%d**\n",pv->start);	
			//printk(add_map(pv->start,pv));
		}else{
			printk("**pv is NULL!!**");
		}	
	}
	return add_map(addr,pv);
}
/**********************************/
#define RAMDISK_DEVICE_NAME "ramdisk"
//static char *ramdisk_name;
#define SECTOR_SIZE 512  
//扇区大小  3*1024*1024
//#define DISK_SIZE (128*1024)  
#define DISK_SIZE (3*1024*1024)  
//虚拟磁盘大小  
#define SECTOR_ALL (DISK_SIZE/SECTOR_SIZE)  
//虚拟磁盘的扇区数  

static int ramdisk_major=0;
// static struct gendisk *p_disk; //用来指向申请的gendisk结构体  
// static struct request_queue *p_queue; //用来指向请求队列  
static unsigned char mem_start[DISK_SIZE]; //分配一块3M 的内存作为虚拟磁盘 
static void *ramdisk_addr;

static void ramdisk_make_request(struct request_queue *q,struct bio *bio)  
{  

printk("--**%p**--\n",mem_start);

struct bio_vec *bvec; //bio结构中包含多个bio_vec结构  
int i; //用于循环的变量，不需要赋值  
int k;
int lenr;
int lenw;
int sizer;
int sizew;
int ramdisk_volume_no;
 void *disk_mem; //指向虚拟磁盘正在读写的位置  
 if((bio->bi_sector*SECTOR_SIZE)+bio->bi_size>DISK_SIZE)  
 { //检查超出容量的情况  
  printk("ramdisk over flowed!\n");  
  bio_endio(bio,1); //第二个参数为1通知内核bio处理出错  
  return ;  
 }

struct ramdisk_private_data *rpd = bio->bi_bdev->bd_disk->private_data;
printk("pmvolume: volume_no %d\n", rpd->volume_no);
ramdisk_volume_no = rpd->volume_no;
//printk("&&&&ramdisk_volume_no:%d&&&&&&\n",ramdisk_volume_no);

//bio_for_each_segment是一个for循环的宏，每次循环处理一个bio_vec  
 bio_for_each_segment(bvec,bio,i){  
  void *iovec; //指向内核存放数据的地址  iovec	

  iovec=kmap(bvec->bv_page)+bvec->bv_offset; //将bv_page映射到高端内存  
printk("-->segment:iovec:%p<--\n",iovec); 
 switch(bio_data_dir(bio)){ //bio_data_dir(biovecio)返回要处理数据的方向  
   case READA: //READA是预读，RAED是读，采用同一iiovecovec处理  
   case READ:
		
		lenr=bvec->bv_len;			

		if(lenr<4096){
			unsigned long addr=volume_addr(ramdisk_volume_no);
			struct page* device_page=pfn_to_page(addr);
			disk_mem=(void *)kmap(device_page)+bio->bi_sector*SECTOR_SIZE;
			memcpy(iovec,disk_mem,bvec->bv_len);
		}else{
			sizer=lenr/4096;
			
			
			for(k=0;k<=sizer;k++){
				unsigned long addr=volume_addr(ramdisk_volume_no)+k;
				struct page* device_page=pfn_to_page(addr);
				disk_mem=(void *)kmap(device_page)+bio->bi_sector*SECTOR_SIZE;
				if(k<sizer){
					memcpy(iovec,disk_mem,4096);
					lenr=lenr-4096;
				}else{
					memcpy(iovec,disk_mem,lenr);
				}
			}
		}
		//memcpy(iovec,disk_mem,bvec->bv_len);
		break;  
   case WRITE:
		
		lenw=bvec->bv_len;
		
		//sizew=lenw/4096;
		if(lenw<4096){
			unsigned long addr=volume_addr(ramdisk_volume_no);
			struct page* device_page=pfn_to_page(addr);
			disk_mem=(void *)kmap(device_page)+bio->bi_sector*SECTOR_SIZE;
			//printk("pmdebug: %p %d\n", virt_to_phys(disk_mem), lenw);
			memcpy(disk_mem,iovec,bvec->bv_len);
		}else{
			sizew=lenw/4096;
			
			for(k=0;k<=sizew;k++){
				unsigned long addr=volume_addr(ramdisk_volume_no)+k;
				struct page* device_page=pfn_to_page(addr);
				disk_mem=(void *)kmap(device_page)+bio->bi_sector*SECTOR_SIZE;
				//printk("pmdebug: %p %d\n", virt_to_phys(disk_mem), lenw);
				if(k<sizew){
					memcpy(disk_mem,iovec,4096);
					lenw=lenw-4096;
				}else{
					memcpy(disk_mem,iovec,lenw);
				}
			}
		}		

		break;  
   default:bio_endio(bio,1);kunmap(bvec->bv_page);return ; //处理失败的情况  
  }  
  kunmap(bvec->bv_page); //释放bv_page的映射  
  disk_mem+=bvec->bv_len; //移动虚拟磁盘的指向位置，准备下一个循环bvec的读写做准备  
 }  
 bio_endio(bio,0); //第二个参数为0通知内核处理成功  
 return ;  
}


static struct block_device_operations ramdisk_fops={  
  .owner=THIS_MODULE,  
}; 

static int init_ramdisk(int volume_no)
{
	printk("pmvolume: init ramdisk %d\n", volume_no);

	struct request_queue *p_queue=blk_alloc_queue(GFP_KERNEL); //申请请求队列，不附加make_request函数 
	if(!p_queue)return -1;  
 	blk_queue_make_request(p_queue,ramdisk_make_request); //将自己编写的make_request函数添加到申请的队列  
	struct gendisk *p_disk = alloc_disk(1); //申请一个分区的gendisk结构体  
 	if(!p_disk)  
 	{  
  		return -1;  
 	}

	//strcpy(p_disk->disk_name,ramdisk_name);
	char namebuf[50] = "";
	snprintf(namebuf, 50, "ramdisk%d", volume_no);
	strcpy(p_disk->disk_name, namebuf);

	ramdisk_major=register_blkdev(0,namebuf);

	if(ramdisk_major<=0){
		printk("******注册失败虚拟设备！******\n");	
		return -1;
	}else{
		struct ramdisk_private_data* pd = (struct ramdisk_private_data*)kmalloc(sizeof(struct ramdisk_private_data), GFP_KERNEL);
		pd->volume_no = volume_no;
		 p_disk->major=ramdisk_major; //主设备号  
 		 p_disk->first_minor=0; //次设备号  
	 	 p_disk->fops=&ramdisk_fops; //fops地址  
 		 p_disk->queue=p_queue; //请求队列地址 
 		 p_disk->private_data = pd;
 		 set_capacity(p_disk,SECTOR_ALL); //设置磁盘扇区数
 		 add_disk(p_disk); //设置好后添加这个磁盘  
 	}
    struct list_head *ph;
    struct pm_ramdisk_blk *p_entry;
    bool is_contain = false;
	list_for_each(ph, &pm_ramdisk_blk_list->list) {
		p_entry = list_entry(ph, struct pm_ramdisk_blk, list);
		if(p_entry!=NULL && p_entry->volume_no==volume_no) {
			is_contain = true;
			break;
		}
	}

	struct pm_ramdisk_blk *entry = NULL;
	if(is_contain) {
		entry = p_entry;
	} else {
		entry = (struct pm_ramdisk_blk*)kmalloc(sizeof(struct pm_ramdisk_blk), GFP_KERNEL);
		list_add_tail(&entry->list, &pm_ramdisk_blk_list->list);
	}
	entry->volume_no = volume_no;
	entry->p_disk = p_disk;
	entry->p_queue = p_queue;

	return 0;  
}


static void del_ramdisk_blk(int volume_no) {

	if(volume_no == -1) {
		struct list_head *p;
		struct pm_ramdisk_blk *p_entry;
        list_for_each(p, &pm_ramdisk_blk_list->list){
           p_entry = list_entry(p, struct pm_ramdisk_blk, list);
		   if(p_entry!=NULL){
		   		del_gendisk(p_entry->p_disk);
		   		put_disk(p_entry->p_disk);
		   		blk_cleanup_queue(p_entry->p_queue);
		   }
		}
	} else {
		struct list_head *p;
		struct pm_ramdisk_blk *p_entry;
        list_for_each(p, &pm_ramdisk_blk_list->list){
                p_entry = list_entry(p, struct pm_ramdisk_blk, list);
		   if(p_entry!=NULL && p_entry->volume_no == volume_no){
		   		del_gendisk(p_entry->p_disk);
		   		put_disk(p_entry->p_disk);
		   		blk_cleanup_queue(p_entry->p_queue);
		   }
		}
	}
}
/*********************************/

int register_module(int volume_num){
	printk("****-->volume_num value is:%d<--****\n",volume_num);
	int res;
	int minor;
	int i;
	//动态分配设备编号
	res=alloc_chrdev_region(&dev0,volume_num,1,devName);	
	if(res<0){
		printk("***fail****\n");	
		return -1;
	}
	else{
		printk("******start register!!!******\n");
		major0=MAJOR(dev0); //主设备号
		minor=MINOR(dev0);  //次设备号
printk("******************&&&&&&&&&&*************\n");
		struct major_list *entry = (struct major_list*)kmalloc(sizeof(struct major_list),GFP_KERNEL);
		entry->major = major0;
printk("&&%p&&\n",entry);
		entry->minor = minor;
		entry->device_no=volume_num;
		list_add_tail(&entry->list, &major_head->list);
		printk("****-->major:%d,minor:%d<--****\n",major0,minor);
	  	
		//动态初始化cdev	
		pm_cdev0=cdev_alloc();		
		pm_cdev0->owner=THIS_MODULE;
		pm_cdev0->ops=&pStruct;

		cdev_add(pm_cdev0,dev0,1);  //把cdev加入到系统中	
		//printk("******register success!!******\n");
		//fpga_class0 = class_create(THIS_MODULE, devName);
		for(i=0; i<1; i++){
			if (IS_ERR(fpga_class0))
			{
				printk("Err:failed in creating class.\n");
				return -1;
			}
			//在/dev目录下创建设备节点
			device_create(fpga_class0, NULL, MKDEV(major0, volume_num), NULL, "PMDevice%d",volume_num);
		}
	}	
	return 0;	
}


int init_volume_list(void){

printk("****init_volume_list***\n");

	pm_volume_arrays =(struct pm_volume*)kmalloc(sizeof(struct pm_volume),GFP_KERNEL);
	memset(pm_volume_arrays, 0, sizeof(struct pm_volume));
	INIT_LIST_HEAD(&pm_volume_arrays->list);
	pm_volume_arrays->volume_no = 0;

	pm_ramdisk_blk_list = (struct pm_ramdisk_blk_list*)kmalloc(sizeof(struct pm_ramdisk_blk), GFP_KERNEL);
	memset(pm_ramdisk_blk_list, 0, sizeof(struct pm_ramdisk_blk));
	INIT_LIST_HEAD(&pm_ramdisk_blk_list->list);
	pm_ramdisk_blk_list->volume_no = 0;

	pm_device_arrays =(struct pm_device*)kmalloc(sizeof(struct pm_device),GFP_KERNEL);	
	memset(pm_device_arrays, 0, sizeof(struct pm_device));

	major_head =(struct major_list*)kmalloc(sizeof(struct major_list),GFP_KERNEL);
	memset(major_head, 0, sizeof(struct major_list));
	INIT_LIST_HEAD(&major_head->list);

	start_head =(struct start_list*)kmalloc(sizeof(struct start_list),GFP_KERNEL);
	memset(start_head, 0, sizeof(struct start_list));
	INIT_LIST_HEAD(&start_head->list);
	return 0;
	
}



int init_module()
{
	printk("******Enter******\n");
	
	int res;
	int minor;
	int i;
	res=alloc_chrdev_region(&dev,0,1,DEVICE_NAME);
	
	if(res<0){
		printk("******register virtual device failed!!******\n");	
		return -1;
	}else{
		printk("******start to register virtual device!!******\n");
		major=MAJOR(dev);
		minor=MINOR(dev);
		printk("major:%d, minor:%d\n",major,minor);
		pm_cdev=cdev_alloc();
		
		pm_cdev->owner=THIS_MODULE;
		pm_cdev->ops=&pStruct;
		cdev_add(pm_cdev,dev,1);

		printk("******success!!******\n");

		//创建一个类
		fpga_class = class_create(THIS_MODULE, DEVICE_NAME);
		if (IS_ERR(fpga_class))
		{
			printk("Err:failed in creating class.\n");
			return -1;
		}	
		//再调用device_create函数来在/dev目录下创建相应的设备节点	
		device_create(fpga_class, NULL, MKDEV(major, 0), NULL, "%s", DEVICE_NAME);

		init_volume_list();
		fpga_class0 = class_create(THIS_MODULE, devName);  //创建class用于创建其他设备

		//申请内存模拟非易失性内存(1000个)
		/*int device_no1[500];
		int k;
		for(k=0;k<500;k++){
			device_no1[k]=k;
		}
		printk("*******\n");
		for(k=0;k<500;k++){
			printk(device_no1[k]+"\n");
		}*/
				
	
		//int device_no[10] = {0,1,2,3,4,5,6,7,8,9};
		//int device_size[10] = {32,32,32,32,32,32,32,32,32,32};
		//int device_num = 3000;
		createDevice(device_nums);
		//createDevice(device_no,device_size,device_num);	
			
		/*struct list_head *p;
		struct pm_device *pv;
     	 	list_for_each(p, &pm_device_arrays->list){
            	pv = list_entry(p, struct pm_device, list);
			if(pv!=NULL){
				printk("**device_no:%d**\n",pv->device_no);
				printk("**size:%d**\n",pv->size);	
			}else{
				printk("**pv is NULL!!**");
			}	
		}*/	
		//printk("$$$%d%%\n",sizeof(char));
	}

	return 0;
}

void cleanup_module()
{
	printk("cleanup_module!!...\n");
	cdev_del(pm_cdev);
	if(pm_cdev0!=NULL){
		cdev_del(pm_cdev0);
	}

      struct list_head *p;
	struct major_list *pl;
        list_for_each(p, &major_head->list){
                pl = list_entry(p, struct major_list, list);
		   if(pl!=NULL){
		   	device_destroy(fpga_class0, MKDEV(pl->major, pl->minor));
		    }        
	}
	
	device_destroy(fpga_class, MKDEV(major, 0));
	class_destroy(fpga_class);
	if(fpga_class0!=NULL){
		class_destroy(fpga_class0);
	}
	unregister_chrdev_region(dev,1);

	del_ramdisk_blk(-1);

printk("!!!!!!!!!!!!!!!!!!!!!\n");
	struct list_head *p1;

	//struct list_head *p1;
	int volume_nos[100];
	int i=0;
	struct pm_volume *pv;
      list_for_each(p1, &pm_volume_arrays->list){
            pv= list_entry(p1, struct pm_volume, list);
		if(pv!=NULL){
			printk("**vloume_no:%d**\n",pv->volume_no);
			volume_nos[i++]=pv->volume_no;
		}else{
			printk("**pv1 is NULL**\n");
		}	
	}

int j;
	for(j=0;j<i;j++){
		//printk("*******%d*******\n",volume_nos[j]);
		deleteVolume(volume_nos[j]);
	}


printk("*****%p\n",&pm_volume_arrays->list);

p1=NULL;
 list_for_each(p1, &pm_volume_arrays->list){
 	printk("&&&&^^^^^&&&&\n");
        
            pv= list_entry(p1, struct pm_volume, list);
		if(pv!=NULL){
			printk("**vloume_no:%d**\n",pv->volume_no);
		}else{
			printk("**pv1 is NULL**\n");
		}	
	}
printk("######1111#######\n");



	struct start_list *sl;
      list_for_each(p1, &start_head->list){
            sl = list_entry(p1, struct start_list, list);
		if(sl!=NULL){
			kfree(sl->start);	
		}else{
			printk("**pv is NULL!!**");
		}	
	}
printk("######################\n");


	kfree(pm_volume_arrays);
	kfree(pm_device_arrays);


/*
	struct pm_volume *pv;
	list_for_each(p1,&pm_volume_arrays->list){
		pv=list_entry(p1,struct pm_volume,list);
		if(pv!=NULL){
			kfree(pv);
		}
	}
	kfree(pm_volume_arrays);
	struct pm_volume *pd;
	list_for_each(p1,&pm_device_arrays->list){
		pd=list_entry(p1,struct pm_device,list);
		if(pd!=NULL){
			kfree(pd);
		}
	}
	kfree(pm_device_arrays);
*/	
	printk("SUCESS>>>>>");
}

MODULE_LICENSE("GPL");

//open device
static int my_open(struct inode *inode, struct file *file)
{
	printk("device is starting!!...\n");
	int num = MINOR(inode->i_rdev);
	printk("****minor value:%d****\n", num);
	printk("****major value:%d****\n",MAJOR(inode->i_rdev));

	if(num==0){
		int i;
		struct pm_volume *p;
		p = pm_volume_arrays;
		struct list_head *lh = &p->list;
		for(i=0; i<num; i++){
			lh = lh->next;
		}
		pmv = container_of(lh,struct pm_volume, list);
	}else{
		struct pm_volume *p;
		struct list_head *lh;
        	list_for_each(lh, &pm_volume_arrays->list){
                	p = list_entry(lh, struct pm_volume, list);
			if(p->volume_no == num){
				break;
                	}
       	 	}
		pmv = p;
	}
printk("*****************************\n");
	struct list_head *p1 =&(pmv->head->list);
	struct pm_device *pos;
	int i;
	for(i=0; i<pmv->num;i++){
		
		p1 = p1->next;
		pos = list_entry(p1, struct pm_device, list);	
		if(i==0){
			pm_device_pfn=pos->start;  //device的起始地址
		}
	}	
printk("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n");
	file->private_data = pmv;
	if(mutex)
                return -EBUSY;
      mutex=1;  //上锁
	try_module_get(THIS_MODULE);
	return 0;
}

//close device
static int my_release(struct inode *inode, struct file *file)
{
	printk("close device...!!\n");

	struct list_head *p;
	struct pm_volume *pv;
      list_for_each(p, &pm_volume_arrays->list){
            pv = list_entry(p, struct pm_volume, list);
		if(pv!=NULL){
			printk("**vloume_no:%d**\n",pv->volume_no);	
		}else{
			printk("**pv is NULL!!**");
		}	
	}	
	
	module_put(THIS_MODULE);
      mutex = 0;//开锁
	return 0;
}

static loff_t pm_lseek(struct file *file, loff_t offset, int orig){
	loff_t ret;
	mutex_lock(&file_inode(file)->i_mutex);
	switch(orig){
		case SEEK_SET:   //从起始位置开始
			file->f_pos=offset;
			ret=file->f_pos;
			break;
		case SEEK_CUR:   //从当前位置开始
			file->f_pos+=offset;
			ret=file->f_pos;
			break;
		case SEEK_END:   //从结尾位置开始(zone_size=device_size)
			file->f_pos=zone_size-offset;
			ret=file->f_pos;
			break;
		default:
			file->f_pos-=offset;
                 	ret =file->f_pos;
	}	
	mutex_unlock(&file_inode(file)->i_mutex);
	return ret;
}

//写(?)
static ssize_t nvm_write(struct file *filp,const char __user *user,size_t t,loff_t *f){

	printk("***WRITE:%d %d\n",filp->f_pos,*f);
	if((pmv->volume_size)*4096-filp->f_pos+1<t){
		printk("--space is not enough!!--\n");
		return -EFAULT;
	}
	char *strW=(char *)kmalloc(sizeof(char)*t,GFP_KERNEL);		
	if(copy_from_user(strW,user,t)){
		return -EFAULT;		
	}
	pm_write(filp,strW,t);
	kfree(strW);
	printk("--**WRITE:file->f_pos:%lu\n",filp->f_pos);
	*f+=t;
	printk("--**WRITE:file->f_pos:%lu\n",filp->f_pos);
	return t;
}

//读
static ssize_t nvm_read(struct file *filp,char __user *user,size_t t,loff_t *f){

	printk("***READ:%d %d %d\n",filp->f_pos,*f,t);
	if((pmv->volume_size)*4096-filp->f_pos+1<t){
		printk("--read fail--");
		return -EFAULT;
	}

	char *strR=(char *)kmalloc(sizeof(char)*t,GFP_KERNEL);	
	pm_read(filp,strR,t);
	int i;
	for(i=0;i<t;i++){
		if(strR[i]!='\0'){
			printk("READ data:%d %c\n",i,strR[i]);
		}	
	}
	if(copy_to_user(user,strR,t)){
		return -EFAULT;
	}	
	kfree(strR);	
	printk("--**READ:file->f_pos:%lu\n",filp->f_pos);
	*f+=t;
	printk("--**READ:file->f_pos:%lu\n",filp->f_pos);
	return t;
}

//确定页
/*
 *start_pos:起始位置
 *start_page:跨页
 *page_pos:起始页中的起始位置
 */
unsigned long p_num=0;  //所需要的页数
struct device_page_use* find_page(struct file *file,unsigned long len){
printk("****find_page:file->f_pos:%lu\n",file->f_pos);
printk("********pmv->start:%lu**\n",pmv->start);
	p_num=0;
	int i;
	loff_t start_pos,start_page,page_pos;
	unsigned long f_len=0; //在第一页中的大小
	unsigned long l_len=0; //在最后一页中的大小
	//unsigned long zone_pfn=ZONE_START_ADDR>>PAGE_SHIFT;  //zone起始地址的pfn
	unsigned long zone_pfn=pmv->start;  //zone(pm_volume)起始地址的pfn
	start_pos=file->f_pos;
printk("--find_page->start_pos:%d--\n",start_pos);
	if((start_pos%4096)==0){ 
		start_page=start_pos/4096;
printk("--find_page->start_page:%d--\n",start_page);
	        page_pos=0;
printk("--find_page->page_pos:%d--\n",page_pos);
		zone_pfn+=start_page;
		l_len=len%4096;
		if(l_len!=0){
			p_num=len/4096+1;
			device_pages=(struct device_page_use*)kmalloc(sizeof(struct device_page_use)*p_num,GFP_KERNEL);
			for(i=0;i<p_num;i++){
				if(i==p_num-1){
					device_pages[i].zone_pfn=zone_pfn+i*1;
					device_pages[i].offset=0;
					device_pages[i].size=l_len;	
				}else{
					device_pages[i].zone_pfn=zone_pfn+i*1;
					device_pages[i].offset=0;
					device_pages[i].size=4096;			
				}
			}	
		}else{
			p_num=len/4096;
			device_pages=(struct device_page_use*)kmalloc(sizeof(struct device_page_use)*p_num,GFP_KERNEL);
			for(i=0;i<p_num;i++){
				device_pages[i].zone_pfn=zone_pfn+i*1;
				device_pages[i].offset=0;
				device_pages[i].size=4096;			
			}
		}
	}else{
		start_page=start_pos/4096;
printk("--find_page->start_page:%d--\n",start_page);
	        page_pos=start_pos%4096;
printk("--find_page->page_pos:%d--\n",page_pos);
		zone_pfn+=start_page;
		if(page_pos+len>4096){
			f_len=4096-page_pos;
			l_len=(len-f_len)%4096;
			if(l_len!=0){
				p_num=(len-f_len)/4096+1+1;
				device_pages=(struct device_page_use*)kmalloc(sizeof(struct device_page_use)*p_num,GFP_KERNEL);
				device_pages[0].zone_pfn=zone_pfn;
				device_pages[0].offset=page_pos;
				device_pages[0].size=f_len;
				for(i=1;i<p_num;i++){
					if(i==p_num-1){
						device_pages[i].zone_pfn=zone_pfn+i*1;
						device_pages[i].offset=0;
						device_pages[i].size=l_len;	
					}else{
						device_pages[i].zone_pfn=zone_pfn+i*1;
						device_pages[i].offset=0;
						device_pages[i].size=4096;			
					}			
				}
			}else{
				p_num=(len-f_len)/4096+1;
				device_pages=(struct device_page_use*)kmalloc(sizeof(struct device_page_use)*p_num,GFP_KERNEL);
				device_pages[0].zone_pfn=zone_pfn;
				device_pages[0].offset=page_pos;
				device_pages[0].size=f_len;
				for(i=1;i<p_num;i++){
					device_pages[i].zone_pfn=zone_pfn+i*1;
					device_pages[i].offset=0;
					device_pages[i].size=4096;			
				}
			}
		}else{
			f_len=len;
			p_num=1;
			device_pages=(struct device_page_use*)kmalloc(sizeof(struct device_page_use)*1,GFP_KERNEL);
			device_pages[0].zone_pfn=zone_pfn;
			device_pages[0].offset=page_pos;
			device_pages[0].size=f_len;
		}
	}
	return device_pages;	
}

void pm_write(struct file *file,void *content,unsigned long len){
printk("--**WRITE**--\n");
printk("---len:%lu---\n",len);
	struct device_page_use*  device_pages=find_page(file,len); 
	unsigned long i;	
	void *vir_addr;
	struct page* device_page;
	unsigned long device_pfn;
	unsigned long length=len;  //还需要写的数据的长度
	unsigned long size=0;  //已经写过的数据的长度
printk("----p_num:%lu----\n",p_num);
	for(i=0;i<p_num;i++){	
		device_pfn=add_map(device_pages[i].zone_pfn,pmv);
		device_page=pfn_to_page(device_pfn);
		vir_addr=(void *)kmap(device_page)+device_pages[i].offset;
printk("---WRITE**device_pfn:%lu--\n",device_pfn);
printk("---WRITE**device_page0:%p--\n",pfn_to_page(device_pfn));
printk("---WRITE**device_page1:%p--\n",device_page);
printk("---WRITE**zone_pfn:%lu--\n",device_pages[i].zone_pfn);
printk("---WRITE**offset:%lu--\n",device_pages[i].offset);
printk("---WRITE**size:%lu--\n",device_pages[i].size);
printk("---WRITE**vir_addr--:%lu--\n",(void *)kmap(device_page));
printk("---WRITE**vir_addr:%lu--\n",vir_addr);


		if(device_pages[i].size==length){
			memcpy(vir_addr,content+size,device_pages[i].size);	
		}else{
			memcpy(vir_addr,content+size,device_pages[i].size);
			size+=device_pages[i].size;
			length-=device_pages[i].size;
		}
	}
	
	kfree(device_pages);
}

void pm_read(struct file *file,void *target,unsigned long len){
printk("--**READ**--\n");
printk("--**READ:file->f_pos:%lu\n",file->f_pos);
	struct device_page_use*  device_pages=find_page(file,len); 
	unsigned long i;	
	void *vir_addr;
	struct page* device_page;
	unsigned long device_pfn;
	unsigned long length=len;  //还需要写的数据的长度
	unsigned long size=0;  //已经写过的数据的长度
printk("----p_num:%lu----\n",p_num);	
	for(i=0;i<p_num;i++){
		device_pfn=add_map(device_pages[i].zone_pfn,pmv);
		device_page=pfn_to_page(device_pfn);
		vir_addr=(void *)kmap(device_page)+device_pages[i].offset;

printk("---READ**device_pfn:%lu---\n",device_pfn);
printk("---READ**device_page0:%p--\n",pfn_to_page(device_pfn));
printk("---READ**device_page1:%p--\n",device_page);
printk("---READ**zone_pfn:%lu--\n",device_pages[i].zone_pfn);
printk("---READ**offset:%lu--\n",device_pages[i].offset);
printk("---READ**size:%lu--\n",device_pages[i].size);
printk("---READ**vir_addr--:%lu--\n",(void *)kmap(device_page));
printk("---READ**vir_addr:%lu--\n",vir_addr);
	
char test[4298]={""};
memcpy(test,vir_addr,1);
printk("--test:%c--\n",test[0]);

		if(device_pages[i].size==length){
			memcpy(target+size,vir_addr,device_pages[i].size);	
		}else{
			memcpy(target+size,vir_addr,device_pages[i].size);
			size+=device_pages[i].size;
			length-=device_pages[i].size;
		}
	}
	kfree(device_pages);
}


//内存映射(device_page->device_pfn->zone_pfn->device_page)
int pm_mmap(struct file *filp, struct vm_area_struct *vma){

    printk("enter in pm_mmap...\n");
    unsigned long vmsize = vma->vm_end-vma->vm_start;
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
//unsigned long prot=vma->vm_page_prot->pgprot;
printk("***vmsize:%lu\n",vmsize);
printk("***offset:%lu\n",offset);
printk("****prot:%lu\n",vma->vm_page_prot);


    unsigned long  prot=(vma->vm_page_prot).pgprot;
printk("****prot:%lu\n",prot);
	int num=0;
	int size=0;
    	int i;
    	int start_pfn;
    	num=vmsize/4096;
		printk("****prot:%lu\n",vma->vm_page_prot);
		
	/*if(prot==39){
	           struct page* zone_page;
		   struct page* page;
		   unsigned long device_pfn;
		   unsigned long zone_pfn;
		   if(num%2==0){
			size=ilog2(num);
		   }else{
			size=ilog2(num)+1;
		   }
printk("**size:%d\n",size);
		   page=alloc_pages(GFP_PM,size);
check_pm_free_area();
		  device_pfn=page_to_pfn(page); //device起始地址的pfn
   		  zone_pfn=add_map_reverse(device_pfn,pmv)-1;  //zone起始地址的pfn
		  for(i=0;i<num;i++){
			zone_pfn=zone_pfn+1;
printk("**zone_pfn:%lu\n",zone_pfn);
			device_pfn=add_map(zone_pfn,pmv);
printk("**device_pfn:%lu\n",device_pfn);
			if(remap_pfn_range(vma,vma->vm_start+4096*i,device_pfn,4096,vma->vm_page_prot))
        			return -EAGAIN;
    		   }
		device_pfn=page_to_pfn(page);
printk("--device_pfn:%lu\n",device_pfn);

		zone_pfn=add_map_reverse(device_pfn,pmv);
printk("--zone_pfn:%lu\n",zone_pfn);
		zone_page=&page_addr[zone_pfn-zone->zone_start_pfn];
printk("--zone_page:%p\n",zone_page);
printk("--size:%d\n",size);
	  	__free_pages(zone_page,size);
check_pm_free_area();
	}else{*/
		 unsigned long device_pfn=pm_device_pfn; //device起始地址的pfn
    		 //unsigned long zone_pfn=add_map_reverse(device_pfn,pmv);  //zone起始地址的pfn
		 unsigned long zone_pfn=pmv->start;  //zone起始地址的pfn
    		 zone_pfn=zone_pfn+(offset/4096)-1;   //offset
    		 for(i=0;i<num;i++){
			zone_pfn=zone_pfn+1;
			device_pfn=add_map(zone_pfn,pmv);
printk("--device_pfn:%lu--\n",device_pfn);
			if(remap_pfn_range(vma,vma->vm_start+4096*i,device_pfn,4096,vma->vm_page_prot))
        			return -EAGAIN;
    		 }
	//}
   
    return 0;
}

/*创建pm_device*/
struct pm_device *createDevice(unsigned int device_num){

	printk("--***create pm_device***--\n");
	int i=0;
	printk("--device_num:%d--\n",device_num);
	
	//for(i=0;i<device_num;i++){
	//	printk("----ENTRY--\n");
	//	printk("--device_no i:%d %d--\n",i,device_no[i]);
	//	printk("--device_size i:%d %d--\n",i,device_size[i]);
	//}
	
	struct pm_device *entry;
	INIT_LIST_HEAD(&(pm_device_arrays->list));
	for(i=0;i<device_num;i++){
		entry=(struct pm_device*)kmalloc(sizeof(struct pm_device),GFP_KERNEL);
		//entry->name = "device";		
		entry->device_no=i;
		entry->size=32;
		void *tmp = kmalloc(sizeof(char)*32*PAGE_SIZE, GFP_KERNEL);
		
		struct start_list *entry1 = (struct start_list*)kmalloc(sizeof(struct start_list),GFP_KERNEL);
		entry1->start = tmp;
		list_add_tail(&(entry1->list),&(start_head->list));		

		entry->start=virt_to_phys(tmp)>>PAGE_SHIFT;
		//printk("**************pfn_device:%lu**************\n",entry->start);
		list_add_tail(&(entry->list),&(pm_device_arrays->list));
	}
	return pm_device_arrays;
}

/*创建pm_device*/
/*struct pm_device *createDevice(int *device_no,unsigned int *device_size,unsigned int device_num){

	printk("--***create pm_device***--\n");
	int i=0;
	printk("--device_num:%d--\n",device_num);
	
	//for(i=0;i<device_num;i++){
	//	printk("----ENTRY--\n");
	//	printk("--device_no i:%d %d--\n",i,device_no[i]);
	//	printk("--device_size i:%d %d--\n",i,device_size[i]);
	//}
	
	struct pm_device *entry;
	INIT_LIST_HEAD(&(pm_device_arrays->list));
	for(i=0;i<device_num;i++){
		entry=(struct pm_device*)kmalloc(sizeof(struct pm_device),GFP_KERNEL);
		//entry->name = "device";		
		entry->device_no=device_no[i];
		entry->size=device_size[i];
	printk("**%d**\n",device_size[i]);
		void *tmp = kmalloc(sizeof(char)*device_size[i]*PAGE_SIZE, GFP_KERNEL);
		
		struct start_list *entry1 = (struct start_list*)kmalloc(sizeof(struct start_list),GFP_KERNEL);
		entry1->start = tmp;
		list_add_tail(&(entry1->list),&(start_head->list));		

		entry->start=virt_to_phys(tmp)>>PAGE_SHIFT;
		//printk("**************pfn_device:%lu**************\n",entry->start);
		list_add_tail(&(entry->list),&(pm_device_arrays->list));
	}
	return pm_device_arrays;
}*/

/*创建pm_volume*/
struct pm_volume *createVolume(int *device_no,int *device_start, unsigned int *device_size,unsigned int device_num,int volume_no,unsigned long start){

	printk("--***create volume***--\n");		
	struct pm_volume *entry=(struct pm_volume*)kmalloc(sizeof(struct pm_volume),GFP_KERNEL);
	int i=0;

	struct pm_device *entry1;
	struct pm_device *p=(struct pm_device*)kmalloc(sizeof(struct pm_device),GFP_KERNEL);
	INIT_LIST_HEAD(&(p->list));
	
	for(i=0;i<device_num;i++){
		entry1=(struct pm_device*)kmalloc(sizeof(struct pm_device),GFP_KERNEL);
		struct pm_device *rightPos = findDevicePos(device_no[i]);		
		entry1->name =rightPos->name;	
		entry1->start =rightPos->start+device_start[i];
		entry1->size =device_size[i];		
		entry1->device_no =device_no[i];
		entry1->connection_type =rightPos->connection_type;			
		entry1->synchronize_type =rightPos->synchronize_type;	
		list_add_tail(&(entry1->list),&(p->list));	
	}

	entry->start=start;
	entry->num=device_num;
	entry->volume_no=volume_no;
	entry->volume_size=0;
	for(i=0;i<device_num;i++){
		entry->volume_size+=device_size[i];
	}	
	
	entry->head=p;
	list_add_tail(&entry->list, &pm_volume_arrays->list);
	printk("**the information of new volume -->volume_no :%d,device_num:%d\n", entry->volume_no,entry->num);
	return pm_volume_arrays;	
}

/*根据设备号查找管理这个设备的pm_device的地址*/
struct pm_device *findDevicePos(int device_no){
	
	printk("--***fineDevicePos***--\n");
      struct list_head *p;
	struct pm_device *pd;
        list_for_each(p, &pm_device_arrays->list){
                pd = list_entry(p, struct pm_device, list);
		if(pd->device_no == device_no){
			return pd;
                }               
        }
	return NULL;
}

/*删除卷*/
int deleteVolume(int volume_no){
        
	printk("--***deleteVolume*:%d**--\n",volume_no);
	

	struct list_head *p1;
	struct pm_volume *pv1;
      list_for_each(p1, &pm_volume_arrays->list){
            pv1 = list_entry(p1, struct pm_volume, list);
		if(pv1!=NULL){
			printk("**vloume_no:%d**\n",pv1->volume_no);
		}else{
			printk("**pv1 is NULL**\n");
		}	
	}	
	printk("***********\n");


	struct list_head *p;
	struct pm_volume *pv;
      list_for_each(p, &pm_volume_arrays->list){
            pv = list_entry(p, struct pm_volume, list);
		if(pv==NULL){
			printk("%%pv is NULLL%%\n");
			break;
		}
		printk("***-->volume_no:%d<--***\n",pv->volume_no);
	      struct list_head *tmp;		
		if(pv->volume_no == volume_no){
            
            printk("***the No of volume is :%d***\n",pv->volume_no);
			tmp=p->prev;
			tmp->next=p->next;                        
			list_del(p);
			p = tmp;
            struct pm_device *pd;
			struct list_head *p1;
		
	        list_for_each(p1, &pv->head->list){
        			pd = list_entry(p1, struct pm_device, list);                        
					printk("--device_no:%d--\n",pd->device_no);
					struct list_head *tmp1;
					tmp1=p1->prev;
					tmp1->next=p1->next;
					list_del(p1);
					p1=tmp1;
					kfree(pd);			
		 	}
	
            kfree(pv);
		   break;
                }
                //break;
        }


struct list_head *p2;
	struct pm_volume *pv2;
      list_for_each(p2, &pm_volume_arrays->list){
            pv2 = list_entry(p2, struct pm_volume, list);
		if(pv2!=NULL){
			printk("**vloume_no:%d**\n",pv2->volume_no);
		}else{
			printk("**pv1 is NULL**\n");
		}	
	}	

        return 0;
}

//计算PM卷前n个卷的总长度
unsigned int sum(unsigned int n, struct pm_volume *_pmv){
	unsigned int sum = 0;
	struct list_head *p =&(_pmv->head->list);
	struct pm_device *pos;
	int i;
	for(i=0; i<n; i++){
		p = p->next;
		pos = list_entry(p, struct pm_device, list);		
		sum += pos->size;
	}
	return sum;
}

//从PMzone映射到相应设备上的算法
unsigned long add_map(unsigned long vol_pfn, struct pm_volume *_pmv){
		
	//printk("--***add_map:把zone的pfn映射到pm_device的pfn***--\n");
	printk("**add_map**\n");
	int i,j;
	struct list_head *p =&(_pmv->head->list);
	struct pm_device *pos;
	p = p->next;
	if(vol_pfn< _pmv->start + sum(1,_pmv)){
		pos = list_entry(p, struct pm_device, list);		
		return vol_pfn - (_pmv->start  - pos->start);	
	}else{		
		for(i=1; i<_pmv->num; i++){
			for(j=0; j<i; j++){				
				p = p->next;
			}			
			if(vol_pfn< _pmv->start + sum(i+1,_pmv)){
				pos = list_entry(p, struct pm_device, list);				
				return vol_pfn - (_pmv->start + sum(i,_pmv) - pos->start);
				break;
			}else {
				p = &(_pmv->head->list);
				p = p->next;
			}
		}
	}
}

unsigned long add_map_reverse(unsigned long dev_pfn, struct pm_volume *_pmv){
       
        //printk("--***add_map_reverse:把pm_device的pfn映射到zone的pfn***--\n");
        printk("**add_map_reverse**\n");
	int i,j;
        struct list_head *p =&(_pmv->head->list);
        struct pm_device *pos;
        p = p->next;
        for(i=0; i<_pmv->num; i++){
            for(j=0; j<i; j++){
	      	p = p->next;
                }
		pos = list_entry(p, struct pm_device, list);

		if((dev_pfn< pos->start + sum(i+1, _pmv)-sum(i,_pmv))&&( dev_pfn >= pos->start)){
			if(i ==0){
				return dev_pfn - pos->start + _pmv->start; 
			}else {
				return dev_pfn - pos->start + _pmv->start + sum(i, _pmv);
			}
			break;	
		}else{
			p = &(_pmv->head->list);
			p = p->next;
		}
	}
}

static long nvm_ctl(struct file *filp,unsigned int cmd,unsigned long arg){

	printk("****-->nvm_ctl<--****\n");
	struct ioc_val val;
	struct device_val device;
	struct vol_val vol;
	
	int vol_n;
	int vol_no;
	int err=0;
	int ret=0;
      int vall=16; 
	int i;       

	/*检查命令的有效性*/
	if(_IOC_TYPE(cmd)!=PMDEV_IOC_MAGIC){
		return -EINVAL; 
	}
	if(_IOC_NR(cmd)>PMDEV_MAX_NR){
		return -EINVAL;
	}
	/*根据命令类型,检测参数空间是否可用*/
	/*方向*/
	if(_IOC_DIR(cmd) & _IOC_READ){
		err=!access_ok(VERIFY_WRITE,(void *)arg,_IOC_SIZE(cmd));
	}	
	else if(_IOC_DIR(cmd) & _IOC_WRITE){
		err=!access_ok(VERIFY_READ,(void *)arg,_IOC_SIZE(cmd));
	}
	if(err)
		return -EINVAL;
	/*根据命令执行相应的操作*/
	switch(cmd){
		/*设置*/
		case SUPPORTED_MODES_SET:

			break;
		case VOLUME_SIZE_SET:
printk("VOLUME_SIZE_SET");
			break;
		case INTERRUPTED_STORE_ATOMICITY_SET:

			break; 
		case FUNDAMENTAL_ERROR_RANGE_SET:

			break;
		case FUNDAMENTAL_ERROR_RANGE_OFFSET_SET:

			break;
		case DISCARD_IF_YOU_CAN_CAPABLE_SET:

			break;
		case DISCARD_IMMEDIATELY_CAPABLE_SET:

			break;
		case DISCARD_IMMEDIATELY_RETURNS_SET:

			break;
		case EXISTS_CAPABLE_SET:

			break;
		/*读取*/	
		case SUPPORTED_MODES_GET:

			break;
		case FILE_MODE_GET:

			break;
		case VOLUME_SIZE_GET:
printk("****-->VOLUME_SIZE_GET<--****\n");

			val.value=pmv->num;
printk("%d\n",val.value);
			if(copy_to_user((struct ioc_val*)arg,&val,sizeof(struct ioc_val))){
				return -EINVAL;			
			}

			break;
		case INTERRUPTED_STORE_ATOMICITY_GET:

			break; 
		case FUNDAMENTAL_ERROR_RANGE_GET:

			break;
		case FUNDAMENTAL_ERROR_RANGE_OFFSET_GET:

			break;
		case DISCARD_IF_YOU_CAN_CAPABLE_GET:

			break;
		case DISCARD_IMMEDIATELY_CAPABLE_GET:

			break;
		case DISCARD_IMMEDIATELY_RETURNS_GET:

			break;
		case EXISTS_CAPABLE_GET:

			break;
		case GET_DEVICE_DETAIL:
printk("****-->GET_DEVICE_DETAIL<--****\n");
			//!? linkedlist
			struct all_device_val device_all;
			//struct all_device_val
			struct list_head *p11 =&(pm_device_arrays->list);	
			struct pm_device *pos11;
			for(i=0; i<device_nums;i++){
				p11 = p11->next;
				pos11 = list_entry(p11, struct pm_device, list);
				
				device_all.size=pos11->size;
				device_all.name=pos11->device_no;
				
				if(copy_to_user(((struct all_device_val*)arg)+i,&device_all,sizeof(struct all_device_val))){
					return -EINVAL;
				}
				}
				
			break;

		case GET_RANGESET:
printk("****-->GET_RANGESET<--****\n");

			struct device_val device_pm;
			struct list_head *p1 =&(pmv->head->list);
			struct pm_device *pos;
			
			for(i=0; i<pmv->num;i++){
				p1 = p1->next;
				pos = list_entry(p1, struct pm_device, list);
				device_pm.start=pos->start;
				device_pm.size=pos->size;
				device_pm.device_no=pos->device_no;
				device_pm.connection_type=pos->connection_type;
				device_pm.synchronize_type=pos->synchronize_type;
				if(copy_to_user(((struct device_val*)arg)+i,&device_pm,sizeof(struct device_val))){
					return -EINVAL;
				}
			}		
			break;

		case CREATEDEVICE:
printk("****-->CREATEDEVICE<--****\n");
			if(copy_from_user(&vol_n,(int *)arg,sizeof(int))){
				return -EINVAL;
			};
			printk("the value of vol_n is %d",vol_n);
			register_module(vol_n);
			init_ramdisk(vol_n);
			break;
		case CREATEVOLUME:
printk("****-->CREATEVOLUME<--****\n");

			if(copy_from_user(&vol,(struct vol_val *)arg,sizeof(struct vol_val))){
				return -EINVAL;
			};

			int *device_no=(int *)kmalloc(sizeof(int)*vol.device_num,GFP_KERNEL);
int *device_start=(int *)kmalloc(sizeof(int)*vol.device_num,GFP_KERNEL);
			int *device_size=(int *)kmalloc(sizeof(int)*vol.device_num,GFP_KERNEL);
			for(i=0;i<vol.device_num;i++){
				get_user(device_no[i],(int *)(vol.device_no+i));
				get_user(device_start[i],(int *)(vol.device_start+i));
				get_user(device_size[i],(int *)(vol.device_size+i));
				
				//init_ramdisk(device_no[i]);
			}		

printk("*******");
int j;
//for(j=0;j<vol.device_num;j++){
//	printk("%p\n",device_start[j]);
//}
			createVolume(device_no,device_start,device_size,vol.device_num,vol.volume_no,vol.start);
			break;

		case DELETEVOLUME:
printk("****-->DELETEVOLUME<--****\n");

			if(copy_from_user(&vol_no,(int *)arg,sizeof(int))){
				return -EINVAL;
			};
printk("****%d****\n",vol_no);
			deleteVolume(vol_no);
printk("***major:%d,vol_no:%d***\n",major0,vol_no);
			//device_destroy(fpga_class0, MKDEV(major0, vol_no));
		
			struct list_head *p;
			struct major_list *pl;
       	 	list_for_each(p, &major_head->list){
                		pl = list_entry(p, struct major_list, list);
		   		if(pl->device_no==vol_no){
		   			device_destroy(fpga_class0, MKDEV(pl->major, pl->minor));
		    		}        
			}
			
			del_ramdisk_blk(vol_no);			

			break;
		//!new 2
		case RAMDISK:
			printk("******create ramdisk******");
			struct list_head *pr;
				struct pm_volume *pvr;
			      list_for_each(pr, &pm_volume_arrays->list){
			            pvr = list_entry(pr, struct pm_volume, list);
					if(pvr!=NULL){
						init_ramdisk(pvr->volume_no);				
					}else{
						printk("**pv is NULL!!**");
					}	
				}
		break;	
		default:
			printk("命令输入错误!!!\n");
			return -EINVAL;

	}
	return ret;
}


