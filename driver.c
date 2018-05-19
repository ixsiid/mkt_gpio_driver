#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <asm/io.h>


/* ペリフェラルレジスタの物理アドレス(BCM2835の仕様書より) */
#define REG_ADDR_BASE		(0x3F000000)	   /* bcm_host_get_peripheral_address()の方がbetter */
#define REG_ADDR_GPIO_BASE   (REG_ADDR_BASE + 0x00200000)
#define REG_ADDR_GPIO_GPFSEL_0	 0x0000
#define REG_ADDR_GPIO_OUTPUT_SET_0 0x001C
#define REG_ADDR_GPIO_OUTPUT_CLR_0 0x0028
#define REG_ADDR_GPIO_LEVEL_0	  0x0034

#define REG(addr) (*((volatile unsigned int*)(addr)))
#define DUMP_REG(addr) printk("%08X\n", REG(addr));

/*** このデバイスに関する情報 ***/
MODULE_LICENSE("Dual BSD/GPL");
#define DRIVER_NAME "MyDevice"			  /* /proc/devices等で表示されるデバイス名 */
static const unsigned int MINOR_BASE = 0;   /* このデバイスドライバで使うマイナー番号の開始番号と個数(=デバイス数) */
static const unsigned int MINOR_NUM  = 1;   /* マイナー番号は 0のみ */
static unsigned int mydevice_major;		 /* このデバイスドライバのメジャー番号(動的に決める) */
static struct cdev mydevice_cdev;		   /* キャラクタデバイスのオブジェクト */
static struct class *mydevice_class = NULL; /* デバイスドライバのクラスオブジェクト */

typedef struct {
	char message[128];
	int length;
	int button;
	int led;
	int value;
} Button;

static Button buttons[2] = {
	{ "load /usr/share/sounds/sf2/TimGM6mb.sf2", -1, -1, -1, -1 },
	{ "select 0 1 0 0", -1, 6, 12, 1 },
	{ "select 0 2 0 16", -1, 13, 16, 1 },
};
static int selected_button = 0;
static int writed_count = 0;

for (int i=0; i<2; i++) {
	if (buttons[i].length >= 0) continue;
	for (buttons[i].length = 0; buttons[i].message[buttons[i].length] != '\0'; buttons[i].length++);
	buttons[i].message[buttons[i].length++] = '\n';
	buttons[i].message[buttons[i].length++] = '\0';
}

/* open時に呼ばれる関数 */
static int fluidsynth_open(struct inode *inode, struct file *file)
{
	printk("fluidsynth controller open");

	/* ARM(CPU)から見た物理アドレス → 仮想アドレス(カーネル空間)へのマッピング */
	int address = (int)ioremap_nocache(REG_ADDR_GPIO_BASE + REG_ADDR_GPIO_GPFSEL_0, 4);

	/* GPIO4を出力に設定 */
	REG(address) = 1 << 12;

	iounmap((void*)address);

	return 0;
}

/* close時に呼ばれる関数 */
static int fluidsynth_close(struct inode *inode, struct file *file)
{
	printk("fluidsynth controller close");
	return 0;
}

static int _latest_value = 0;
static int _index = 0;
char message[3] = {'0', '\n', '\0'};
/* read時に呼ばれる関数 */
static ssize_t device_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	if (selected_button < 0) {

	// ARM(CPU)から見た物理アドレス → 仮想アドレス(カーネル空間)へのマッピング
	int address = address = (int)ioremap_nocache(REG_ADDR_GPIO_BASE + REG_ADDR_GPIO_LEVEL_0, 4);
	int val = (REG(address) & (1 << 4)) != 0; // GPIO4が0かどうかを0, 1にする

		iounmap((void*)address);
	}

	if (selected_button < 0) {
		put_user(0, &buf[0]);
		return 1;
	}

	int len = buttons[selected_button].length - writed_count;
	if (count >= len) {
		copy_to_user(buf, &buttons[selected_button].message[writed_count], len);
		selected_button = -1;
		return len;
	} else {
		copy_to_user(buf, &buttons[selected_button].message[writed_count], count);
		writed_count += count;
		return count;
	}
	return -1;
}

/* write時に呼ばれる関数 */
static ssize_t device_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	int address;
	char outValue;

	/* ユーザが設定したGPIOへの出力値を取得 */
	get_user(outValue, &buf[0]);

	/* ARM(CPU)から見た物理アドレス → 仮想アドレス(カーネル空間)へのマッピング */
	if(outValue == '1') {
		/* '1'ならSETする */
		address = (int)ioremap_nocache(REG_ADDR_GPIO_BASE + REG_ADDR_GPIO_OUTPUT_SET_0, 4);
	} else {
		/* '0'ならCLRする */
		address = (int)ioremap_nocache(REG_ADDR_GPIO_BASE + REG_ADDR_GPIO_OUTPUT_CLR_0, 4);
	}
	REG(address) = 1 << 4;
	iounmap((void*)address);

	return count;
}

/* 各種システムコールに対応するハンドラテーブル */
struct file_operations s_mydevice_fops = {
	.open	= fluidsynth_open,
	.release = fluidsynth_close,
	.read	= device_read,
	.write   = device_write,
};

/* ロード(insmod)時に呼ばれる関数 */
static int mydevice_init(void)
{
	printk("mydevice_init\n");

	int alloc_ret = 0;
	int cdev_err = 0;
	dev_t dev;

	/* 1. 空いているメジャー番号を確保する */
	alloc_ret = alloc_chrdev_region(&dev, MINOR_BASE, MINOR_NUM, DRIVER_NAME);
	if (alloc_ret != 0) {
		printk(KERN_ERR  "alloc_chrdev_region = %d\n", alloc_ret);
		return -1;
	}

	/* 2. 取得したdev( = メジャー番号 + マイナー番号)からメジャー番号を取得して保持しておく */
	mydevice_major = MAJOR(dev);
	dev = MKDEV(mydevice_major, MINOR_BASE);	/* 不要? */

	/* 3. cdev構造体の初期化とシステムコールハンドラテーブルの登録 */
	cdev_init(&mydevice_cdev, &s_mydevice_fops);
	mydevice_cdev.owner = THIS_MODULE;

	/* 4. このデバイスドライバ(cdev)をカーネルに登録する */
	cdev_err = cdev_add(&mydevice_cdev, dev, MINOR_NUM);
	if (cdev_err != 0) {
		printk(KERN_ERR  "cdev_add = %d\n", alloc_ret);
		unregister_chrdev_region(dev, MINOR_NUM);
		return -1;
	}

	/* 5. このデバイスのクラス登録をする(/sys/class/mydevice/ を作る) */
	mydevice_class = class_create(THIS_MODULE, "mydevice");
	if (IS_ERR(mydevice_class)) {
		printk(KERN_ERR  "class_create\n");
		cdev_del(&mydevice_cdev);
		unregister_chrdev_region(dev, MINOR_NUM);
		return -1;
	}

	/* 6. /sys/class/mydevice/mydevice* を作る */
	for (int minor = MINOR_BASE; minor < MINOR_BASE + MINOR_NUM; minor++) {
		device_create(mydevice_class, NULL, MKDEV(mydevice_major, minor), NULL, "mydevice%d", minor);
	}

	return 0;
}

/* アンロード(rmmod)時に呼ばれる関数 */
static void mydevice_exit(void)
{
	printk("mydevice_exit\n");

	dev_t dev = MKDEV(mydevice_major, MINOR_BASE);

	/* 7. /sys/class/mydevice/mydevice* を削除する */
	for (int minor = MINOR_BASE; minor < MINOR_BASE + MINOR_NUM; minor++) {
		device_destroy(mydevice_class, MKDEV(mydevice_major, minor));
	}

	/* 8. このデバイスのクラス登録を取り除く(/sys/class/mydevice/を削除する) */
	class_destroy(mydevice_class);

	/* 9. このデバイスドライバ(cdev)をカーネルから取り除く */
	cdev_del(&mydevice_cdev);

	/* 10. このデバイスドライバで使用していたメジャー番号の登録を取り除く */
	unregister_chrdev_region(dev, MINOR_NUM);
}

module_init(mydevice_init);
module_exit(mydevice_exit);

