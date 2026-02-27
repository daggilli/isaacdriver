#ifndef ISAACDRIVER_H__
#define ISAACDRIVER_H__
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/param.h>

#define RANDBITS 8
#define RANDSIZE 256

const size_t OPBUFSIZE = 4096;
const size_t OPBUFLEN = OPBUFSIZE * sizeof(u32);

static int major = 0;
static int bswap = 0;

module_param(major, int, 0444);  // read-only
module_param(bswap, int, 0444);

#define ind(mm, x) (*(u32 *)((u8 *)(mm) + ((x) & ((RANDSIZE - 1) << 2))))
#define rngstep(mixit, a, b, mm, m, m2, r, x) \
  {                                           \
    x = *m;                                   \
    a = (a ^ (mixit)) + *(m2++);              \
    *(m++) = y = ind(mm, x) + a + b;          \
    *(r++) = b = ind(mm, y >> RANDBITS) + x;  \
  }
#define mix(a, b, c, d, e, f, g, h) \
  {                                 \
    a ^= b << 11;                   \
    d += a;                         \
    b += c;                         \
    b ^= c >> 2;                    \
    e += b;                         \
    c += d;                         \
    c ^= d << 8;                    \
    f += c;                         \
    d += e;                         \
    d ^= e >> 16;                   \
    g += d;                         \
    e += f;                         \
    e ^= f << 10;                   \
    h += e;                         \
    f += g;                         \
    f ^= g >> 4;                    \
    a += f;                         \
    g += h;                         \
    g ^= h << 8;                    \
    b += g;                         \
    h += a;                         \
    h ^= a >> 9;                    \
    c += h;                         \
    a += b;                         \
  }

struct isaac {
  u32 randcnt;
  u32 randa;
  u32 randb;
  u32 randc;
  u32 *randrsl;
  u32 *randmem;
};

struct isaacdev {
  struct mutex lockmx;
  struct isaac isaacrng;
  struct cdev cdev;
  u16 cdevok;
  dev_t dev;
  u16 devok;
  u32 *opbuf;
  size_t phase;
  u32 bswap;
  struct class *iscclass;
  struct device *iscdev;
};

static struct isaacdev idev;

typedef u32 (*randgetf)(struct isaac *);

randgetf randget;

static int isaac_open(struct inode *inode, struct file *filep);
static ssize_t isaac_read(struct file *filep, char __user *buf, size_t len, loff_t *ppos);
static ssize_t isaac_write(struct file *filep, const char __user *buf, size_t count, loff_t *f_pos);

static ssize_t bswap_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t bswap_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static char *isaac_devnode(const struct device *dev, umode_t *mode);

static u32 randisc(struct isaac *isc);
static u32 randisc_byterev(struct isaac *isc);
static void isaac(struct isaac *isc);
static void randinit(struct isaac *isc);

static int __init isaac_init(void);
static void __exit isaac_exit(void);
static void cleanup(struct isaacdev *idevp);

#endif
