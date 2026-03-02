#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/slab.h>

#include "isaacdriver.h"

/*********************************************************************/
/*********************************************************************/
//
// FILE OPERATIONS
//
/*********************************************************************/
/*********************************************************************/

static int isaac_open(struct inode *inode, struct file *filep) {
  struct isaacdev *idevp;

  idevp = container_of(inode->i_cdev, struct isaacdev, cdev);
  filep->private_data = idevp;

  return nonseekable_open(inode, filep);
}

static ssize_t isaac_read(struct file *filep, char __user *buf, size_t len, loff_t *ppos) {
  size_t i, j, headresidue, tailresidue, bigblks, blks, tlen = len;
  ssize_t readres = 0;
  struct isaacdev *idevp = filep->private_data;

  if (tlen == 0) return 0;

  if (mutex_lock_interruptible(&idevp->lockmx)) return -ERESTARTSYS;

  headresidue = (sizeof(u32) - idevp->phase) % sizeof(u32);
  if (tlen <= headresidue) headresidue = tlen;

  u64 ctures;

  if (headresidue > 0) {
    if ((ctures = copy_to_user(buf, ((char *)idevp->opbuf) + idevp->phase, headresidue)) < 0)
      readres = -EFAULT;
    else {
      readres += headresidue;
      tlen -= headresidue;
      idevp->phase += headresidue;
      idevp->phase %= sizeof(u32);
      if (idevp->phase == 0) idevp->opbuf[0] = randget(&(idevp->isaacrng));
    }
  }

  if (readres >= 0) {
    bigblks = tlen / OPBUFLEN;
    blks = (tlen % OPBUFLEN) / sizeof(u32);
    tailresidue = tlen % sizeof(u32);

    if (bigblks != 0) {
      for (j = 1; j < OPBUFSIZE; j++) {
        idevp->opbuf[j] = randget(&(idevp->isaacrng));
      }
      if ((ctures = copy_to_user(buf + readres, idevp->opbuf, OPBUFLEN)) < 0) {
        readres = -EFAULT;
      } else
        readres += OPBUFLEN;
    }
  }

  if (readres >= 0) {
    for (i = 1; i < bigblks; i++) {
      for (j = 0; j < OPBUFSIZE; j++) {
        idevp->opbuf[j] = randget(&(idevp->isaacrng));
      }
      if ((ctures = copy_to_user(buf + readres, idevp->opbuf, OPBUFLEN)) < 0) {
        readres = -EFAULT;
        break;
      } else
        readres += OPBUFLEN;
    }
    if (i > 1) idevp->opbuf[0] = randget(&(idevp->isaacrng));
  }

  if (readres >= 0) {
    for (i = 0; i < blks; i++) {
      // phase must be zero to enter here
      if ((ctures = copy_to_user(buf + readres, idevp->opbuf, sizeof(u32))) < 0) {
        readres = -EFAULT;
        break;
      } else
        readres += sizeof(u32);

      idevp->opbuf[0] = randget(&(idevp->isaacrng));
    }
  }

  if ((readres >= 0) && (tailresidue > 0)) {
    if (copy_to_user(buf + readres, idevp->opbuf, tailresidue) != 0)
      readres = -EFAULT;
    else {
      readres += tailresidue;
      idevp->phase += tailresidue;
    }
  }

  mutex_unlock(&idevp->lockmx);

  return readres;
}

static ssize_t isaac_write(struct file *filep, const char __user *buf, size_t len, loff_t *f_pos) {
  size_t tlen = len;
  ssize_t writeres = 0;
  struct isaacdev *idevp = filep->private_data;

  if (tlen > RANDSIZE * sizeof(u32)) tlen = RANDSIZE * sizeof(u32);

  if (mutex_lock_interruptible(&idevp->lockmx)) return -ERESTARTSYS;

  memset(idev.isaacrng.randrsl, 0, RANDSIZE * sizeof(u32));

  if (tlen != 0) {
    if (copy_from_user(idevp->isaacrng.randrsl, buf, tlen) != 0) writeres = -EFAULT;
  }

  if (writeres == 0) {
    randinit(&(idevp->isaacrng));
    idevp->phase = 0;
    idevp->opbuf[0] = randget(&(idevp->isaacrng));
    writeres = tlen;
  }

  mutex_unlock(&idevp->lockmx);

  return writeres;
}

static ssize_t bswap_show(struct device *dev, struct device_attribute *attr, char *buf) {
  struct isaacdev *idevp = (struct isaacdev *)dev_get_drvdata(dev);
  int pres;

  if (mutex_lock_interruptible(&idevp->lockmx)) return 0;

  pres = snprintf(buf, PAGE_SIZE, "%u\n", idevp->bswap);

  mutex_unlock(&idevp->lockmx);

  return pres;
}

static ssize_t bswap_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
  struct isaacdev *idevp = (struct isaacdev *)dev_get_drvdata(dev);
  int bsstoreres;
  unsigned int tmpbswap;

  if ((bsstoreres = kstrtouint(buf, 0, &tmpbswap)) < 0) return bsstoreres;
  if (tmpbswap != 0) tmpbswap = 1;

  if (mutex_lock_interruptible(&idevp->lockmx)) return 0;

  idevp->bswap = tmpbswap;
  randget = (idevp->bswap == 1 ? randisc_byterev : randisc);

  mutex_unlock(&idevp->lockmx);

  return count;
}

static DEVICE_ATTR_RW(bswap);

static char *isaac_devnode(const struct device *dev, umode_t *mode) {
  if (mode) *mode = 0666;
  return NULL;
}

struct file_operations isaac_fops = {
    .owner = THIS_MODULE,
    .open = isaac_open,
    .read = isaac_read,
    .write = isaac_write,
    .llseek = no_llseek,
};

/*********************************************************************/
/*********************************************************************/
//
// ISAAC FUNCTIONS
//
/*********************************************************************/
/*********************************************************************/

static u32 randisc(struct isaac *isc) {
  if (isc->randcnt-- == 0) {
    isaac(isc);
    isc->randcnt = RANDSIZE - 1;
  }
  return isc->randrsl[isc->randcnt];
}

static u32 randisc_byterev(struct isaac *isc) {
  if (isc->randcnt-- == 0) {
    isaac(isc);
    isc->randcnt = RANDSIZE - 1;
  }
#ifdef __LITTLE_ENDIAN
  return cpu_to_be32(isc->randrsl[isc->randcnt]);
#else
  return cpu_to_le32(isc->randrsl[isc->randcnt]);
#endif
}

static void isaac(struct isaac *isc) {
  u32 a, b, x, y, *m, *mm, *m2, *r, *mend;

  a = isc->randa;
  b = isc->randb + (++(isc->randc));
  mm = isc->randmem;
  r = isc->randrsl;

  for (m = mm, mend = m2 = m + (RANDSIZE / 2); m < mend;) {
    rngstep(a << 13, a, b, mm, m, m2, r, x);
    rngstep(a >> 6, a, b, mm, m, m2, r, x);
    rngstep(a << 2, a, b, mm, m, m2, r, x);
    rngstep(a >> 16, a, b, mm, m, m2, r, x);
  }
  for (m2 = mm; m2 < mend;) {
    rngstep(a << 13, a, b, mm, m, m2, r, x);
    rngstep(a >> 6, a, b, mm, m, m2, r, x);
    rngstep(a << 2, a, b, mm, m, m2, r, x);
    rngstep(a >> 16, a, b, mm, m, m2, r, x);
  }
  isc->randb = b;
  isc->randa = a;
}

static void randinit(struct isaac *isc) {
  u16 i;
  u32 a = 0x9E3779B9, b = 0x9E3779B9, c = 0x9E3779B9, d = 0x9E3779B9, e = 0x9E3779B9, f = 0x9E3779B9,
      g = 0x9E3779B9, h = 0x9E3779B9, *m = isc->randmem, *r = isc->randrsl;

  isc->randa = 0;
  isc->randb = 0;
  isc->randc = 0;

  for (i = 0; i < 4; ++i) /* scramble it */
    mix(a, b, c, d, e, f, g, h);

  for (i = 0; i < RANDSIZE; i += 8) {
    a += r[i];
    b += r[i + 1];
    c += r[i + 2];
    d += r[i + 3];
    e += r[i + 4];
    f += r[i + 5];
    g += r[i + 6];
    h += r[i + 7];
    mix(a, b, c, d, e, f, g, h);
    m[i] = a;
    m[i + 1] = b;
    m[i + 2] = c;
    m[i + 3] = d;
    m[i + 4] = e;
    m[i + 5] = f;
    m[i + 6] = g;
    m[i + 7] = h;
  }

  for (i = 0; i < RANDSIZE; i += 8) {
    a += m[i];
    b += m[i + 1];
    c += m[i + 2];
    d += m[i + 3];
    e += m[i + 4];
    f += m[i + 5];
    g += m[i + 6];
    h += m[i + 7];
    mix(a, b, c, d, e, f, g, h);
    m[i] = a;
    m[i + 1] = b;
    m[i + 2] = c;
    m[i + 3] = d;
    m[i + 4] = e;
    m[i + 5] = f;
    m[i + 6] = g;
    m[i + 7] = h;
  }

  isaac(isc);
  isc->randcnt = RANDSIZE;
}

/*********************************************************************/
/*********************************************************************/
//
// INIT AND EXIT FUNCTIONS
//
/*********************************************************************/
/*********************************************************************/

static int __init isaac_init(void) {
  dev_t dev;
  int initres = 0;

  unsigned int isaacmajor = 0, isaacminor = 0;

  memset(&idev, 0, sizeof(struct isaacdev));

  pr_info("isaac kernel init\n");

  if (major == 0) {
    initres = alloc_chrdev_region(&dev, isaacminor, 1, "isaac");
  } else {
    dev = MKDEV(major, isaacminor);
    initres = register_chrdev_region(dev, 1, "isaac");
  }

  if (initres < 0) {
    pr_err("isaac driver: get chrdev_region failed %d\n", initres);
    return initres;
  }

  isaacmajor = MAJOR(dev);

  idev.dev = dev;
  idev.devok = 1;
  pr_info("register %d %d %ld %ld\n", isaacmajor, isaacminor, OPBUFSIZE, OPBUFLEN);

  if ((idev.isaacrng.randrsl = (u32 *)kmalloc(RANDSIZE * sizeof(u32), GFP_KERNEL)) == NULL) {
    cleanup(&idev);
    pr_err("isaac driver: kmalloc randrsl failed\n");
    return -ENOMEM;
  }
  if ((idev.isaacrng.randmem = (u32 *)kmalloc(RANDSIZE * sizeof(u32), GFP_KERNEL)) == NULL) {
    cleanup(&idev);
    pr_err("isaac driver: kmalloc randmem failed\n");
    return -ENOMEM;
  }
  memset(idev.isaacrng.randrsl, 0, RANDSIZE * sizeof(u32));
  if ((idev.opbuf = (u32 *)kmalloc(OPBUFLEN, GFP_KERNEL)) == NULL) {
    cleanup(&idev);
    pr_err("isaac driver: kmalloc randmem failed\n");
    return -ENOMEM;
  }

  // bswap = 1;

  if (bswap)
    randget = randisc_byterev;
  else
    randget = randisc;

  randinit(&idev.isaacrng);

  idev.opbuf[0] = randget(&idev.isaacrng);
  idev.phase = 0;
  idev.bswap = bswap;
  mutex_init(&idev.lockmx);
  cdev_init(&idev.cdev, &isaac_fops);
  idev.cdev.owner = THIS_MODULE;
  idev.cdev.ops = &isaac_fops;
  initres = cdev_add(&idev.cdev, dev, 1);
  if (initres != 0) {
    cleanup(&idev);
    pr_err("isaac driver: cdev_add failed %d\n", initres);
  }
  idev.cdevok = 1;

  idev.iscclass = class_create("rng");
  if (IS_ERR(idev.iscclass)) {
    initres = PTR_ERR(idev.iscclass);
    idev.iscclass = NULL;
    cleanup(&idev);
    return initres;
  }
  idev.iscclass->devnode = isaac_devnode;

  idev.iscdev = device_create(idev.iscclass, NULL, dev, NULL, "isaac");
  if (IS_ERR(idev.iscdev)) {
    initres = PTR_ERR(idev.iscdev);
    idev.iscdev = NULL;
    class_destroy(idev.iscclass);
    idev.iscclass = NULL;
    cleanup(&idev);
    return initres;
  }

  dev_set_drvdata(idev.iscdev, &idev);

  initres = device_create_file(idev.iscdev, &dev_attr_bswap);
  if (initres < 0) {
    cleanup(&idev);
    initres = -ENODEV;
    return initres;
  }
  idev.devattrok = 1;

  return 0;
}

static void __exit isaac_exit(void) {
  cleanup(&idev);
  pr_info("isaac driver exit\n");
}

static void cleanup(struct isaacdev *idevp) {
  if (idevp->isaacrng.randmem != NULL) kfree(idevp->isaacrng.randmem);
  if (idevp->isaacrng.randrsl != NULL) kfree(idev.isaacrng.randrsl);
  if (idevp->opbuf != NULL) kfree(idevp->opbuf);
  if (idevp->devattrok) device_remove_file(idevp->iscdev, &dev_attr_bswap);
  if (idevp->cdevok) cdev_del(&idevp->cdev);
  if (idevp->iscclass != NULL) {
    device_destroy(idevp->iscclass, idevp->dev);
    class_destroy(idevp->iscclass);
  }
  if (idevp->devok) unregister_chrdev_region(idevp->dev, 1);
}

module_init(isaac_init);
module_exit(isaac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Gillies");
MODULE_DESCRIPTION("ISAAC CSPRING driver module");