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
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/hardirq.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <asm/io.h>

/*** このデバイスに関する情報 ***/
MODULE_LICENSE("Dual BSD/GPL");
#define DRIVER_NAME "FluidSynthController"  /* /proc/devices等で表示されるデバイス名 */
static const unsigned int MINOR_BASE = 0;   /* このデバイスドライバで使うマイナー番号の開始番号と個数(=デバイス数) */
static const unsigned int MINOR_NUM = 1;	/* マイナー番号は 0のみ */
static unsigned int mydevice_major;			/* このデバイスドライバのメジャー番号(動的に決める) */
static struct cdev mydevice_cdev;			/* キャラクタデバイスのオブジェクト */
static struct class *mydevice_class = NULL; /* デバイスドライバのクラスオブジェクト */

typedef struct
{
	char message[128];
	int length;
	int pinButton;
	int pinLed;
	int irq;
} Button;

#define BUTTON_COUNT 3

static Button buttons[BUTTON_COUNT] = {
	{"load /usr/share/sounds/sf2/TimGM6mb.sf2", -1, -1, -1, -1},
	{"select 0 1 0 0", -1, 6, 12, -1},
	{"select 0 2 0 16", -1, 13, 16, -1},
};
static int selected_button = 0;
static int next_selected = -1;
static int writed_count = 0;

/* 割り込み発生時(= ボタンが押されたとき)に呼ばれる関数 */
static irqreturn_t gpio_intr(int irq, void *dev_id)
{
	printk("fluidsynth_controller_gpio_intr\n");

	int index = -1;
	for (int i = 0; i < BUTTON_COUNT; i++)
	{
		if (buttons[i].irq == irq)
		{
			index = i;
			break;
		}
	}
	if (index < 0)
	{
		printk("not found irq\n");
		return IRQ_HANDLED;
	}

	int value = gpio_get_value(buttons[index].pinButton);
	gpio_set_value(buttons[index].pinLed, value);
	printk("button %d = %d\n", buttons[index].pinButton, value);
	if (value > 0)
		next_selected = index;

	return IRQ_HANDLED;
}

/* open時に呼ばれる関数 */
static int fluidsynth_open(struct inode *inode, struct file *file)
{
	printk("fluidsynth controller open");

	for (int i = 0; i < BUTTON_COUNT; i++)
	{
		/**GPIOピンの入出力設定
		   LEDピンを出力に設定 */
		if (buttons[i].pinLed >= 0)
			gpio_direction_output(buttons[i].pinLed, 0);

		if (buttons[i].pinButton >= 0)
		{
			/* BUTTONピンを入力に設定 */
			gpio_direction_input(buttons[i].pinButton);
			/* BUTTONの割り込み処理を設定 */
			buttons[i].irq = gpio_to_irq(buttons[i].pinButton);
			if (request_irq(buttons[i].irq, (void *)gpio_intr,
							IRQF_SHARED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
							"fluidsynth_controller_gpio_intr", (void *)gpio_intr) < 0)
			{
				printk(KERN_ERR "request_irq\n");
				return -1;
			}
		}
	}

	return 0;
}

/* close時に呼ばれる関数 */
static int fluidsynth_close(struct inode *inode, struct file *file)
{
	printk("fluidsynth controller close");

	for (int i = 0; i < BUTTON_COUNT; i++)
	{
		if (buttons[i].pinButton >= 0)
		{
			free_irq(buttons[i].irq, (void *)gpio_intr);
		}
	}
	return 0;
}

/* read時に呼ばれる関数 */
static ssize_t device_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	if (selected_button < 0)
	{
		put_user(0, &buf[0]);

		selected_button = next_selected;
		next_selected = -1;
		writed_count = 0;
		return 1;
	}

	int len = buttons[selected_button].length - writed_count;
	if (count >= len)
	{
		copy_to_user(buf, &buttons[selected_button].message[writed_count], len);
		selected_button = next_selected;
		next_selected = -1;
		writed_count = 0;
		return len;
	}
	else
	{
		copy_to_user(buf, &buttons[selected_button].message[writed_count], count);
		writed_count += count;
		return count;
	}
	return -1;
}

/* 各種システムコールに対応するハンドラテーブル */
struct file_operations s_mydevice_fops = {
	.open = fluidsynth_open,
	.release = fluidsynth_close,
	.read = device_read,
	.write = NULL,
};

/* ロード(insmod)時に呼ばれる関数 */
static int mydevice_init(void)
{
	printk("fluidsynth_controller_init\n");

	int alloc_ret = 0;
	int cdev_err = 0;
	dev_t dev;

	/* 1. 空いているメジャー番号を確保する */
	alloc_ret = alloc_chrdev_region(&dev, MINOR_BASE, MINOR_NUM, DRIVER_NAME);
	if (alloc_ret != 0)
	{
		printk(KERN_ERR "alloc_chrdev_region = %d\n", alloc_ret);
		return -1;
	}

	/* 2. 取得したdev( = メジャー番号 + マイナー番号)からメジャー番号を取得して保持しておく */
	mydevice_major = MAJOR(dev);
	dev = MKDEV(mydevice_major, MINOR_BASE); /* 不要? */

	/* 3. cdev構造体の初期化とシステムコールハンドラテーブルの登録 */
	cdev_init(&mydevice_cdev, &s_mydevice_fops);
	mydevice_cdev.owner = THIS_MODULE;

	/* 4. このデバイスドライバ(cdev)をカーネルに登録する */
	cdev_err = cdev_add(&mydevice_cdev, dev, MINOR_NUM);
	if (cdev_err != 0)
	{
		printk(KERN_ERR "cdev_add = %d\n", alloc_ret);
		unregister_chrdev_region(dev, MINOR_NUM);
		return -1;
	}

	/* 5. このデバイスのクラス登録をする(/sys/class/mydevice/ を作る) */
	mydevice_class = class_create(THIS_MODULE, "mydevice");
	if (IS_ERR(mydevice_class))
	{
		printk(KERN_ERR "class_create\n");
		cdev_del(&mydevice_cdev);
		unregister_chrdev_region(dev, MINOR_NUM);
		return -1;
	}

	/* 6. /sys/class/mydevice/mydevice* を作る */
	for (int minor = MINOR_BASE; minor < MINOR_BASE + MINOR_NUM; minor++)
	{
		device_create(mydevice_class, NULL, MKDEV(mydevice_major, minor), NULL, "mydevice%d", minor);
	}

	for (int i = 0; i < BUTTON_COUNT; i++)
	{
		if (buttons[i].length >= 0)
			continue;
		for (buttons[i].length = 0; buttons[i].message[buttons[i].length] != '\0'; buttons[i].length++)
			;
		buttons[i].message[buttons[i].length++] = '\n';
		buttons[i].message[buttons[i].length] = '\0';
	}

	return 0;
}

/* アンロード(rmmod)時に呼ばれる関数 */
static void mydevice_exit(void)
{
	printk("fluidsynth_controller_exit\n");

	dev_t dev = MKDEV(mydevice_major, MINOR_BASE);

	/* 7. /sys/class/mydevice/mydevice* を削除する */
	for (int minor = MINOR_BASE; minor < MINOR_BASE + MINOR_NUM; minor++)
	{
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
