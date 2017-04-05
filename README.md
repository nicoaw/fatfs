# fatfs

A 32-bit FAT filesystem in a file. This filesystem runs in userspace using FUSE and is contained in a file on the host filesystem.

## Getting Started

### Dependencies

* GCC
* CMake version >= 2.6
* FUSE

### Installing

Clone repository

```
$ git clone https://github.com/nicoaw/fatfs.git
$ cd fatfs
```

Build

```
$ cmake ./
$ make
```

Install

```
$ sudo make install
```

Example usage

```
$ mkdir mnt
$ fatfs format disk            # Initialize file called "disk" with an empty fatfs filesystem 
$ fatfs mount disk mnt    # Now "mnt" is the root directory of the fatfs filesystem in "disk"
...
$ sudo umount mnt         # Unmount filesystem when done
```

## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details

## Acknowledgments

* The inspiration for the project comes from an operating systems class assignment
