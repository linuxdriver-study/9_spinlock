#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/ide.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/types.h>

#define DEVICE_CNT              1
#define DEVICE_NAME             "led"
#define LED_ON                  1
#define LED_OFF                 0

struct led_device_struct {
        int major;
        int minor;
        dev_t devid;
        struct cdev led_cdev;
        struct class *class;
        struct device *device;
        struct device_node *nd;
        int led_gpio;
        int dev_status;
        spinlock_t lock;
};
static struct led_device_struct led_dev;

static int led_open(struct inode *nd, struct file *file);
static ssize_t led_write(struct file *file,
                          const char __user *user,
                          size_t size,
                          loff_t *loff);
static int led_release(struct inode *nd, struct file *file);

static struct file_operations ops = {
        .owner = THIS_MODULE,
        .open = led_open,
        .write = led_write,
        .release = led_release,
};

static int led_open(struct inode *nd, struct file *file)
{
        int ret = 0;
        struct led_device_struct *dev = NULL;
        unsigned long flags;

        file->private_data = &led_dev;
        dev = file->private_data;

        spin_lock_irqsave(&dev->lock, flags);
        if (led_dev.dev_status != 0) {
                ret = -EBUSY;
                spin_unlock_irqrestore(&dev->lock, flags);
                goto error;
        }
        dev->dev_status ++;
        spin_unlock_irqrestore(&dev->lock, flags);

error:
        return ret;
}

static ssize_t led_write(struct file *file,
                          const char __user *user,
                          size_t size,
                          loff_t *loff)
{
        int ret = 0;
        unsigned char buf[1] = {0};
        struct led_device_struct *dev = file->private_data;

        ret = copy_from_user(buf, user, 1);
        if (ret != 0) {
                ret = -EFAULT;
                goto error;
        }
        
        if (buf[0] != LED_OFF && buf[0] != LED_ON) {
                ret = -EFAULT;
                goto error;
        }

        if (buf[0] == LED_OFF) {
                gpio_set_value(dev->led_gpio, 1);
        } else if (buf[0] == LED_ON) {
                gpio_set_value(dev->led_gpio, 0);
        }

error:
        return ret;
}

static int led_release(struct inode *nd, struct file *file)
{
        int ret = 0;
        unsigned long flags;
        struct led_device_struct *dev = file->private_data;

        spin_lock_irqsave(&dev->lock, flags);
        dev->dev_status --;
        spin_unlock_irqrestore(&dev->lock, flags);

        file->private_data = NULL;
        return ret;
}

static int __init led_init(void)
{
        int ret = 0;
        if (led_dev.major == 0) {
                ret = alloc_chrdev_region(&led_dev.devid, 0, DEVICE_CNT, DEVICE_NAME);
        } else {
                led_dev.devid = MKDEV(led_dev.major, 0);
                ret = register_chrdev_region(led_dev.devid, DEVICE_CNT, DEVICE_NAME);
        }
        if (ret < 0) {
                printk("chrdev region error!\n");
                goto fail_region;
        }
        led_dev.major = MAJOR(led_dev.devid);
        led_dev.minor = MINOR(led_dev.devid);
        printk("major:%d minor:%d\n", led_dev.major, led_dev.minor);

        cdev_init(&led_dev.led_cdev, &ops);
        ret = cdev_add(&led_dev.led_cdev, led_dev.devid, DEVICE_CNT);
        if (ret < 0) {
                printk("cdev add error!\n");
                goto fail_cdev_add;
        }

        led_dev.class = class_create(THIS_MODULE, DEVICE_NAME);
        if (IS_ERR(led_dev.class)) {
                printk("class create error!\n");
                ret = -EFAULT;
                goto fail_class_create;
        }

        led_dev.device = device_create(led_dev.class, NULL,
                                       led_dev.devid, NULL, DEVICE_NAME);
        if (IS_ERR(led_dev.device)) {
                printk("device create error!\n");
                ret = -EFAULT;
                goto fail_device_create;
        }

        led_dev.nd = of_find_node_by_path("/gpioled");
        if (led_dev.nd == NULL) {
                printk("find node error!\n");
                ret  = -EFAULT;
                goto fail_find_node;
        }
        led_dev.led_gpio = of_get_named_gpio(led_dev.nd, "led-gpios", 0);
        if (led_dev.led_gpio < 0) {
                printk("get named gpio error!\n");
                ret = -EFAULT;
                goto fail_get_named;
        }
        ret = gpio_request(led_dev.led_gpio, "led");
        if (ret != 0) {
                printk("gpio_request error!\n");
                goto fail_request;
        }
        ret = gpio_direction_output(led_dev.led_gpio, 1);
        if (ret < 0) {
                printk("gpio dir set error!\n");
                goto fail_dir_set;
        }
        gpio_set_value(led_dev.led_gpio, 1);

        spin_lock_init(&led_dev.lock);
        led_dev.dev_status = 0;
        goto success;

fail_dir_set:
        gpio_free(led_dev.led_gpio);
fail_request:
fail_get_named:
fail_find_node:
        device_destroy(led_dev.class, led_dev.devid);
fail_device_create:
        class_destroy(led_dev.class);
fail_class_create:
fail_cdev_add:
        unregister_chrdev_region(led_dev.devid, DEVICE_CNT);
fail_region:
success:
        return ret;
}

static void __exit led_exit(void)
{
        gpio_set_value(led_dev.led_gpio, 1);
        gpio_free(led_dev.led_gpio);
        device_destroy(led_dev.class, led_dev.devid);
        class_destroy(led_dev.class);
        unregister_chrdev_region(led_dev.devid, DEVICE_CNT);
}

module_init(led_init);
module_exit(led_exit);
MODULE_AUTHOR("wanglei");
MODULE_LICENSE("GPL");
