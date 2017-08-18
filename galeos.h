#ifndef __GALEOS_H__
#define __GALEOS_H__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/spi/spi.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>

#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/wait.h>

#include <linux/kdev_t.h>

typedef struct galeos_data_s {
  struct list_head  device_entry;
  spinlock_t  spin_lock;
  struct mutex spi_lock;
  struct device *device;
  struct spi_device *spi;
  struct spi_message  spi_message;
  struct spi_transfer spi_transfer[2];
  struct workqueue_struct *workqueue;
  dev_t devt;
  u8  spi_data[2];
  unsigned  spi_speed_hz;
  /* Modem gpio's*/
  int gpio_ac;
  int gpio_reset;
  int gpio_irq;
  int gpio_rdy;
  /* IRQ*/
  int irq;
  /* register */
  unsigned int reg;
  int id;
} galeosdev_data_t;

typedef struct {
  struct work_struct work;
  struct galeos_data *gdata;
  cycles_t cycles;
} galeos_work_t;
#define GALEOS_MAJOR       1
#define GALEOS_MAX_DEVICES 10
#define GALEOS_DRIVER_NAME "shdsl-bNv"
#define GALEOS_MODULE_NAME "shdsl"
#define GALEOS_DEVICE_NAME "shdsl"
#define GALEOS_CLASS_NAME  "galeos"
#define GALEOS_WORKQUEUE_NAME GALEOS_CLASS_NAME "-" GALEOS_DRIVER_NAME

#define GALEOS_FILE_SETTINGS "/etc/galeos/default.conf"

#define GALEOS_DRIVER_VERSION_MAJ 0
#define GALEOS_DRIVER_VERSION_MIN 2


#endif//__GALEOS_H__
