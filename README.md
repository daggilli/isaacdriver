# ISAACDRIVER

## A Linux device driver that provides the ISAAC stream cipher as a cryptographically-secure pseudorandom number generator

### Introduction

ISAAC (indirection, shift, accumulate, add, and count) is a stream cipher created by Robert J. Jenkins that can also be used as a cryptographically-secure pseudorandom number generator. It has an exceedingly long cycle time and is vulnerable to no known practical cryptographic attacks. It is very fast, on the order of generators specifically designed for efficiency (which might not be cryptographically secure).

### Build instructions

Install the Linux kernel headers appropriate for your system.

Install `bear` with your distro's package manager to provide utilities that aid the building of Linux kernel modules. Create the `compile_commands.json` file with the following:

```bash
bear -- make -C /lib/modules/$(uname -r)/build M=$PWD modules
```

Make the kernel module with `make all`. Insert it into the kernel with `sudo insmod isaac.ko`. The character device will appear as `/dev/isaac`.

### Usage

The device can be read from in the normal manner. Before first use after loading, it should be seeded. Its initial seed is set to all zeroes; this is likely not what you want. The seed is provided by writing 1024 (256 x 4) bytes to the device _e.g._

```bash
dd if=/dev/urandom of=/dev/isaac bs=4 count=256
```

Pseudorandom bytes are internally stored as 32-bit integers and are emitted with host endianness. If a byte-swapped output is required, then the parameter `bswap=1` can be passed to the kernel on module insertion, or the driver can be configured through its `sysfs` interface _e.g._ `echo -n 1 > /sys/class/rng/isaac/bswap`.
