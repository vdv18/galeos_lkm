#include <linux/init.h>             // Macros used to mark up functions e.g., __init __exit
#include <linux/module.h>           // Core header for loading LKMs into the kernel
#include <linux/fs.h>

#include "galeos.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitriy Vakhrushev");
MODULE_DESCRIPTION("The galeos shdsl modem Linux driver.");
MODULE_VERSION("0.1");

static int users = 0;
static int major = 0;
static unsigned int reg = 0;

module_param( major, int, S_IRUGO );
static DECLARE_BITMAP(minors, 32);

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

static struct workqueue_struct *workqueue;
static struct class *galeos_class;
static struct of_device_id galeos_of_match[] = {
  { .compatible = "galeos,shdsl-b4v", },
  { .compatible = "galeos,shdsl-b2v", },
  {}
};
MODULE_DEVICE_TABLE(of, galeos_of_match);

static void galeos_register_write( galeosdev_data_t *dev, u8 reg, u8 value )
{
  struct spi_device *spi = dev->spi;
  struct spi_transfer *t = &dev->spi_transfer[0];
  struct spi_message  *m = &dev->spi_message;
  u8 * data = dev->spi_data;
  data[0] = (reg & 0x7F);

  mutex_lock(&dev->spi_lock);

  gpio_set_value(dev->gpio_ac,0);

  spi_message_init(m);
  t->cs_change = 1;
  t->tx_buf = &data[0];
  t->len = 1;
  t->speed_hz = dev->spi_speed_hz;
  spi_message_add_tail(t, m);
  spi_sync(spi,m);

  gpio_set_value(dev->gpio_ac,1);

  spi_message_init(m);
  t->cs_change = 0;
  data[0] = (value & 0xFF);
  t->tx_buf = &data[0];
  t->len = 1;
  t->speed_hz = dev->spi_speed_hz;
  spi_message_add_tail(t, m);
  spi_sync(spi,m);

  mutex_unlock(&dev->spi_lock);
}

static u8 galeos_register_read( galeosdev_data_t *dev, u8 reg)
{
  struct spi_device *spi = dev->spi;
  struct spi_transfer *t = &dev->spi_transfer[0];
  struct spi_message  *m = &dev->spi_message;
  u8 * data = dev->spi_data;
  data[0] = (reg | 0x80);

  mutex_lock(&dev->spi_lock);

  gpio_set_value(dev->gpio_ac,0);

  spi_message_init(m);
  t->cs_change = 1;
  t->tx_buf = &data[0];
  t->len = 1;
  t->speed_hz = dev->spi_speed_hz;
  spi_message_add_tail(t, m);
  spi_sync(spi,m);

  gpio_set_value(dev->gpio_ac,1);

  spi_message_init(m);
  t->cs_change = 0;
  t->tx_buf = &data[0];
  t->rx_buf = &data[1];
  t->len = 1;
  t->speed_hz = dev->spi_speed_hz;
  spi_message_add_tail(t, m);
  spi_sync(spi,m);

  mutex_unlock(&dev->spi_lock);

  return data[1];
}

static ssize_t show_driver_version(struct device *dev, struct device_attribute *attr, char *buf)
{
  return scnprintf(buf, PAGE_SIZE, "%d.%d\n", GALEOS_DRIVER_VERSION_MAJ, GALEOS_DRIVER_VERSION_MIN);
}

static ssize_t show_device_version(struct device *dev, struct device_attribute *attr, char *buf)
{
  galeosdev_data_t *device_data = dev_get_drvdata(dev);
  u8 version = galeos_register_read(device_data, 0x61);
  return scnprintf(buf, PAGE_SIZE, "0x%02X\n", (int)(version));
}

static ssize_t show_device_type(struct device *dev, struct device_attribute *attr, char *buf)
{
  galeosdev_data_t *device_data = dev_get_drvdata(dev);
  u8 type = galeos_register_read(device_data, 0x60);
  return scnprintf(buf, PAGE_SIZE, "0x%02X\n", (int)type);
}

static ssize_t show_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
  return scnprintf(buf, PAGE_SIZE, "%02X\n", reg);
}

static ssize_t store_reg(struct device *dev, struct device_attribute *attr, 
                         const char *buf, size_t count)
{
  sscanf(buf,"%02X",&reg);
  return strlen(buf);;
}

static ssize_t show_data(struct device *dev, struct device_attribute *attr, char *buf)
{
  galeosdev_data_t *device_data = dev_get_drvdata(dev);
  u8 data =  galeos_register_read(device_data, (u8)(reg&0xFF));
  return scnprintf(buf, PAGE_SIZE, "%02X\n", (int)data);
}

static ssize_t store_data(struct device *dev, struct device_attribute *attr,
                          const char *buf, size_t count)
{
  unsigned int data;
  galeosdev_data_t *device_data = dev_get_drvdata(dev);
  sscanf(buf,"%02X",&data);
  galeos_register_write(device_data, (u8)(reg&0xFF), (u8)(data&0xFF));
  return strlen(buf);;
}

static DEVICE_ATTR(driver_version, S_IRUGO, show_driver_version, 0);
static DEVICE_ATTR(device_version, S_IRUGO, show_device_version, 0);
static DEVICE_ATTR(device_type, S_IRUGO, show_device_type, 0);
static DEVICE_ATTR(reg, S_IRUGO | S_IWUSR, show_reg, store_reg);
static DEVICE_ATTR(data, S_IRUGO | S_IWUSR, show_data, store_data);

static struct attribute *dev_attrs[] = {
  /* current configuration's attributes */
  &dev_attr_driver_version.attr,
  &dev_attr_device_version.attr,
  &dev_attr_device_type.attr,
  &dev_attr_reg.attr,
  &dev_attr_data.attr,
  NULL,
};


static ssize_t show_speed(struct device *dev, struct device_attribute *attr, char *buf)
{
  u8  page;
  int data;
  galeosdev_data_t *device_data = dev_get_drvdata(dev);
  page = galeos_register_read(device_data, (u8)(0x7F));
  if(strcmp(attr->attr.name, "speed0") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x00);
  }
  else if(strcmp(attr->attr.name, "speed1") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x01);
  }
  else if(strcmp(attr->attr.name, "speed2") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x02);
  }
  else if(strcmp(attr->attr.name, "speed3") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x03);
  }
  data = galeos_register_read(device_data, (u8)(0x02)) * 64;
  data += galeos_register_read(device_data, (u8)(0x03)) * 8;
  galeos_register_write(device_data, 0x7F,page);
  return scnprintf(buf, PAGE_SIZE, "%d\n", (int)data);
}

static ssize_t store_speed(struct device *dev, struct device_attribute *attr,
                          const char *buf, size_t count)
{
  unsigned int data;
  galeosdev_data_t *device_data = dev_get_drvdata(dev);
  u8  page;
  page = galeos_register_read(device_data, (u8)(0x7F));
  if(strcmp(attr->attr.name, "speed0") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x00);
  }
  else if(strcmp(attr->attr.name, "speed1") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x01);
  }
  else if(strcmp(attr->attr.name, "speed2") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x02);
  }
  else if(strcmp(attr->attr.name, "speed3") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x03);
  }
  sscanf(buf,"%d",&data);
  galeos_register_write(device_data, 0x02, data/64);
  galeos_register_write(device_data, 0x03, (data%64)/8);

  galeos_register_write(device_data, 0x7F,page);
  return strlen(buf);
}

static DEVICE_ATTR(speed0, S_IRUGO | S_IWUSR, show_speed, store_speed);
static DEVICE_ATTR(speed1, S_IRUGO | S_IWUSR, show_speed, store_speed);
static DEVICE_ATTR(speed2, S_IRUGO | S_IWUSR, show_speed, store_speed);
static DEVICE_ATTR(speed3, S_IRUGO | S_IWUSR, show_speed, store_speed);

static ssize_t show_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
  u8  page;
  int data;
  galeosdev_data_t *device_data = dev_get_drvdata(dev);
  page = galeos_register_read(device_data, (u8)(0x7F));
  if(strcmp(attr->attr.name, "mode0") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x00);
  }
  else if(strcmp(attr->attr.name, "mode1") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x01);
  }
  else if(strcmp(attr->attr.name, "mode2") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x02);
  }
  else if(strcmp(attr->attr.name, "mode3") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x03);
  }
  data = galeos_register_read(device_data, (u8)(0x01));
  galeos_register_write(device_data, 0x7F,page);
  switch(data){
    case 0:
    {
      return scnprintf(buf, PAGE_SIZE, "COT\n", (int)data);
    }break;
    case 1:
    {
      return scnprintf(buf, PAGE_SIZE, "RTA\n", (int)data);
    }break;
  }
  return scnprintf(buf, PAGE_SIZE, "off\n", (int)data);
}

static ssize_t store_mode(struct device *dev, struct device_attribute *attr,
                          const char *buf, size_t count)
{
  unsigned int data;
  galeosdev_data_t *device_data = dev_get_drvdata(dev);
  u8  page;
  page = galeos_register_read(device_data, (u8)(0x7F));
  if(strcmp(attr->attr.name, "mode0") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x00);
  }
  else if(strcmp(attr->attr.name, "mode1") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x01);
  }
  else if(strcmp(attr->attr.name, "mode2") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x02);
  }
  else if(strcmp(attr->attr.name, "mode3") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x03);
  }
  if(strncmp(buf,"COT",3) == 0 || strncmp(buf,"cot",3) == 0)
  {
    galeos_register_write(device_data, 0x01, 0x00);
  }
  else if(strncmp(buf, "RTA",3) == 0 || strncmp(buf, "rta",3) == 0)
  {
    galeos_register_write(device_data, 0x01, 0x01);
  }
  else
  {
    galeos_register_write(device_data, 0x01, 0xFF);
  }
  galeos_register_write(device_data, 0x7F,page);
  return strlen(buf);
}

static DEVICE_ATTR(mode0, S_IRUGO | S_IWUSR, show_mode, store_mode);
static DEVICE_ATTR(mode1, S_IRUGO | S_IWUSR, show_mode, store_mode);
static DEVICE_ATTR(mode2, S_IRUGO | S_IWUSR, show_mode, store_mode);
static DEVICE_ATTR(mode3, S_IRUGO | S_IWUSR, show_mode, store_mode);

static ssize_t show_pam(struct device *dev, struct device_attribute *attr, char *buf)
{
  u8  page;
  int data;
  galeosdev_data_t *device_data = dev_get_drvdata(dev);
  page = galeos_register_read(device_data, (u8)(0x7F));
  if(strcmp(attr->attr.name, "pam0") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x00);
  }
  else if(strcmp(attr->attr.name, "pam1") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x01);
  }
  else if(strcmp(attr->attr.name, "pam2") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x02);
  }
  else if(strcmp(attr->attr.name, "pam3") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x03);
  }
  data = galeos_register_read(device_data, (u8)(0x0C));
  galeos_register_write(device_data, 0x7F,page);
  return scnprintf(buf, PAGE_SIZE, "%d\n", (int)data);
}

static ssize_t store_pam(struct device *dev, struct device_attribute *attr,
                          const char *buf, size_t count)
{
  unsigned int data;
  galeosdev_data_t *device_data = dev_get_drvdata(dev);
  u8  page;
  page = galeos_register_read(device_data, (u8)(0x7F));
  if(strcmp(attr->attr.name, "pam0") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x00);
  }
  else if(strcmp(attr->attr.name, "pam1") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x01);
  }
  else if(strcmp(attr->attr.name, "pam2") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x02);
  }
  else if(strcmp(attr->attr.name, "pam3") == 0)
  {
    galeos_register_write(device_data, 0x7F, 0x03);
  }
  if(strlen(buf)>4)
  {
    if(strncmp(buf,"AUTO",4) == 0 || strncmp(buf,"auto",4) == 0)
    {
      galeos_register_write(device_data, 0x0C, 0x00);
      return strlen(buf);
    }
  }
  sscanf(buf,"%d",&data);
  galeos_register_write(device_data, 0x0C, (u8)data);
  galeos_register_write(device_data, 0x7F,page);
  return strlen(buf);
}

static DEVICE_ATTR(pam0, S_IRUGO | S_IWUSR, show_pam, store_pam);
static DEVICE_ATTR(pam1, S_IRUGO | S_IWUSR, show_pam, store_pam);
static DEVICE_ATTR(pam2, S_IRUGO | S_IWUSR, show_pam, store_pam);
static DEVICE_ATTR(pam3, S_IRUGO | S_IWUSR, show_pam, store_pam);

static struct attribute *dev_dsl_attrs[] = {
  /* current configuration's attributes */
  &dev_attr_speed0.attr,
  &dev_attr_speed1.attr,
  &dev_attr_speed2.attr,
  &dev_attr_speed3.attr,
  &dev_attr_mode0.attr,
  &dev_attr_mode1.attr,
  &dev_attr_mode2.attr,
  &dev_attr_mode3.attr,
  &dev_attr_pam0.attr,
  &dev_attr_pam1.attr,
  &dev_attr_pam2.attr,
  &dev_attr_pam3.attr,
  NULL,
};

static struct attribute_group dev_attr_grp = {
  .attrs = dev_attrs,
};

static struct attribute_group dev_dsl_attr_grp = {
  .name  = (const char*)"DSL",
  .attrs = dev_dsl_attrs,
};

static const struct attribute_group *dev_attr_grps[] = {
  &dev_attr_grp,
  &dev_dsl_attr_grp,
  NULL
};

static int galeosspidev_probe(struct spi_device *spi)
{
  unsigned long  minor;
  galeosdev_data_t *device_data;
  void *ptr;
  int status,number;
  // Check device is present
  printk(KERN_EMERG "Galeos SPI Driver Probe...\n");
  if (spi->dev.of_node && !of_match_device(galeos_of_match, &spi->dev)) {
    dev_err(&spi->dev, "buggy DT: device_data listed directly in DT\n");
    WARN_ON(spi->dev.of_node && !of_match_device(galeos_of_match, &spi->dev));
  }
  // Allocate memory for structure of device data
  device_data = kzalloc(sizeof(*device_data), GFP_KERNEL);
  if(!device_data)
    return  -ENOMEM;
  // Assign spi device
  device_data->spi = spi_dev_get(spi);;
  // Assign workqueue
  if(workqueue)
    device_data->workqueue = workqueue;
  // Register device
  mutex_lock(&device_list_lock);
  minor = find_first_zero_bit(minors, GALEOS_MAX_DEVICES);
  if (minor < GALEOS_MAX_DEVICES) {
    device_data->devt = MKDEV(GALEOS_MAJOR, minor);
    //device_data->device = device_create(galeos_class, &(spi->dev), device_data->devt, NULL, GALEOS_DEVICE_NAME"%d.%d",spi->master->bus_num, spi->chip_select);
    device_data->device = device_create_with_groups(galeos_class, &(spi->dev), device_data->devt, device_data, dev_attr_grps, GALEOS_DEVICE_NAME"%d.%d",spi->master->bus_num, spi->chip_select);
    status = PTR_ERR_OR_ZERO(device_data->device);
    if(IS_ERR(device_data->device))
    {
      kfree(device_data);
      return status;
    }
    dev_set_drvdata(device_data->device, device_data);
    spi_set_drvdata(spi, device_data);
  } else {
    dev_dbg(&spi->dev, "no minor number available!\n");
    status = -ENODEV;
    kfree(device_data);
    return status;
  }
  if (status == 0) {
    set_bit(minor, minors);
    list_add(&device_data->device_entry, &device_list);
  }
  mutex_unlock(&device_list_lock);
  // SPI speed
  ptr = of_get_property(spi->dev.of_node, "galeos,spi-speed", NULL);
  if (! IS_ERR(ptr) )
  {
    device_data->spi_speed_hz = (unsigned)be32_to_cpup(ptr);
  }
  else
  {
    device_data->spi_speed_hz = 1000000;
  }
  // Find and register GPIO-s for controling device  
  number = of_gpio_named_count(spi->dev.of_node, "galeos,gpio-ac");
  if(! IS_ERR(number))
  {
    device_data->gpio_ac = of_get_named_gpio(spi->dev.of_node, "galeos,gpio-ac", 0);
    gpio_request(device_data->gpio_ac,"galeos-gpio-ac");
    gpio_export(device_data->gpio_ac,true);
    gpio_direction_output(device_data->gpio_ac,1);
    gpio_set_value(device_data->gpio_ac,1);
  } else device_data->gpio_ac = 0;
  number = of_gpio_named_count(spi->dev.of_node, "galeos,gpio-reset");
  if(! IS_ERR(number))
  {
    device_data->gpio_reset = of_get_named_gpio(spi->dev.of_node, "galeos,gpio-reset", 0);
    gpio_request(device_data->gpio_reset,"galeos-gpio-reset");
    gpio_export(device_data->gpio_reset,true);
    gpio_direction_output(device_data->gpio_reset,1);
    gpio_set_value(device_data->gpio_reset,1);
  } else device_data->gpio_reset = 0;
  number = of_gpio_named_count(spi->dev.of_node, "galeos,gpio-irq");
  if(! IS_ERR(number))
  {
    device_data->gpio_irq = of_get_named_gpio(spi->dev.of_node, "galeos,gpio-irq", 0);
    gpio_request(device_data->gpio_irq,"galeos-gpio-irq");
    gpio_export(device_data->gpio_irq,true);
    gpio_direction_input(device_data->gpio_irq);
  } else device_data->gpio_irq = 0;
  number = of_gpio_named_count(spi->dev.of_node, "galeos,gpio-rdy");
  printk("Number gpio-rdy %d\n",number);
  if(! IS_ERR(number))
  {
    printk("Number gpio-rdy %d\n",number);
    device_data->gpio_rdy = of_get_named_gpio(spi->dev.of_node, "galeos,gpio-rdy", 0);
    gpio_request(device_data->gpio_rdy,"galeos-gpio-rdy");
    gpio_export(device_data->gpio_rdy,true);
    gpio_direction_input(device_data->gpio_rdy);
  } else device_data->gpio_rdy = 0;

  if(1)
  {
    struct file *f;
    char *buff;
    size_t n;
#define BUF_LEN 256
    buff = kmalloc( BUF_LEN, GFP_KERNEL );
    strcpy( buff, "/etc/galeos/default.conf" );
    f = filp_open(buff, O_RDONLY, 0);
    if( ! IS_ERR( f ) ) {
      printk("File is opened");
      n = kernel_read( f, 0, buff, BUF_LEN );
      printk("Read file: %s", buff);
      filp_close( f, NULL );
    }
    kfree(buff);
  }
  mutex_init(&device_data->spi_lock);
//  if (status == 0)
//    spi_set_drvdata(spi, device_data);
//  else

  return status;
}

static int daleosspidev_remove(struct spi_device *spi)
{
  galeosdev_data_t *device_data = spi_get_drvdata(spi);
  /* make sure ops on existing fds can abort cleanly */
  printk(KERN_EMERG "Galeos SPI Driver Remove...\n");
  spi_dev_put(spi);
  spin_lock_irq(&device_data->spin_lock);
  device_data->spi = NULL;
  spin_unlock_irq(&device_data->spin_lock);

  if(device_data->gpio_ac)
	  gpio_free(device_data->gpio_ac);
  if(device_data->gpio_reset)
	  gpio_free(device_data->gpio_reset);
  if(device_data->gpio_irq)
	  gpio_free(device_data->gpio_irq);
  if(device_data->gpio_rdy)
	  gpio_free(device_data->gpio_rdy);

  /* prevent new opens */
  mutex_lock(&device_list_lock);
  list_del(&device_data->device_entry);
  device_destroy(galeos_class, device_data->devt);
  clear_bit(MINOR(device_data->devt), minors);
  if (users == 0)
  	kfree(device_data);
  mutex_unlock(&device_list_lock);
  return 0;
}

static struct spi_driver galeos_spi_driver = {
  .probe = galeosspidev_probe,
  .remove = daleosspidev_remove,
  .driver = {
    .name = GALEOS_DRIVER_NAME,
    .owner = THIS_MODULE,
    .of_match_table = galeos_of_match,
  },
};

static int __init galeos_init(void){
  int ret = 0;
  dev_t dev;
  struct device *device;
  printk(KERN_EMERG "Galeos Driver initialize (init)...\n");
  workqueue = create_workqueue( GALEOS_WORKQUEUE_NAME );
  if(IS_ERR(workqueue))
  {
    printk(KERN_EMERG "Galeos Driver init Workqueue faild...\n");
    return -1;
  }
  printk(KERN_EMERG "Galeos WorkQueue created\n");
  galeos_class = class_create(THIS_MODULE, GALEOS_CLASS_NAME);
  if(IS_ERR(galeos_class))
  {
    printk(KERN_EMERG "Galeos Driver create class faild...\n");
    flush_workqueue( workqueue );
    return -1;
  }
  printk(KERN_EMERG "Galeos Class created\n");
  ret = spi_register_driver(&galeos_spi_driver);
  printk(KERN_EMERG "Galeos driver registered\n");
  if(IS_ERR(ret))
  {
    printk(KERN_EMERG "Galeos Driver register spi driver faild...\n");
    flush_workqueue( workqueue );
    destroy_workqueue( workqueue );
    class_destroy(galeos_class);
    return ret;
  }
  return 0;
}


static void __exit galeos_exit(void){
  printk(KERN_EMERG "Galeos Driver deinicialize (exit)...\n");
  flush_workqueue( workqueue );
  destroy_workqueue( workqueue );
  spi_unregister_driver(&galeos_spi_driver);
  class_destroy(galeos_class);
}

module_init(galeos_init);
module_exit(galeos_exit);
