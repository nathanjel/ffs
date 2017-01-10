# FFS - Fixed File System

A very simple file system solution for ESP32, esp-idf based projects that need a file system over a limited flash space.

# Basic facts

* Static (fixed) file system - files, their location and maximum size are declared at build time
* Very simple to use
* Suitable for projects where flash space is limited, but stdio.h file interface is requested (e.g. by other existing libraries)
* Integrated with esp-idf build system
* Integrated with esp-idf flash partition system
* Integrated with esp-idf virtual file system, in singleton mode
* Supports menuconfig configuration
* Utilizes flash memory mapping where possible, and SPI flash read/write commands elsewhere
* No support for directories
* No wear leveling
* No flash stored filesystem description block, all file data kept in code
* (Yet) no cross-checking of partition sizes vs file sizes vs other limits during configuration.

# Watch-outs

* Only tested under linux. Not tested in Windows and on Mac (yet)
* This is closely coupled with esp-idf for ESP32 and requires it to run.

# Quickstart

0. Go to your components folder and add the ffs submodule using `git submodule add https://github.com/nathanjel/ffs`
1. Copy esp-idf/components/partition_singleapp.csv into Your project folder
2. Update the copied partition_singleapp.csv to fit Your needs, e.g. add a line `data,		data,	0xA0,		,			0x8000`
3. `make menuconfig`
4. Go to Partition table -> Partition table, select Custom partition table CSV 
5. Go to Partition table -> Custom partition CSV file, and set it to partition_singleapp.csv
6. Go to Component config -> FFS (Fixed File system) -> Enable Fixed File System
7. Create a folder called `files` in your project and put there few small files (make sure to keep the files, grow boundary and partition size in mind, FFS will not control this for You (yet))
8. Add includes in Your code `#include <stdio.h>` and `#include "ffs.h"`
9. Add somewhere at the start the initialization call `ffs_initialize();`
10. Use Your files, e.g. 
`FILE * f = fopen("/mount/files/device.cfg", "r");
int res = fread(&Configuration, sizeof(Configuration_t), 1, f);
fclose(f);`
11. Build and make your project as usual, at the end do `make ffs-flash` to flash Your files in place

# Configuring your project

`make menuconfig`

Go to Component config -> FFS (Fixed File system)

Configuration options:

* Mount point for ESP VFS integration
Choose the "mount" folder for Your filesystem.
* Path to folder with files to be uploaded, relative to Project Path
The name of the folder where the files are to be found, relative to project. In the default configuration, just create a directory called files, put your files in. If You will add subfolders, it will be respected (e.g. you can access file /mount/files/folder/file if "folder" is a folder in Your flash files directory, but there is no directory listing support etc.)
* ESP Partition name to load the files into (exactly as in partition file!)
Name of the esp partition to load files into (as in partition file, so the defaul 'data' will work with the example in Quickstart guide).
* Default grow boundary
Marks the default maximum size round up value, e.g. with boundary of 10KB, a 3KB file will be allowed to grow up to 10KB, 21 KB file will be allowed to grow up to 30KB, this controls the pre-reservation of space in Your partition.
* Additional options to file loader
This allows You to specifically control how the files are loaded, the size they have to grow and specific load location, even outside the partition. You can specify for multiple files, separated by space.

1. Specify loading point
<file_name>=[p:<partition_name>,o:<offset_within_partition>]

Loads a file into a specific partition and offset, Use "max" for file specific grow value for the whole partition to be used. Do not use the partition set in menuconfig. This is good for OTA.

2. Loads file at specific offset in flash memory.
<file_name>=r:<raw_flash_offset>

3. Control grow size for files
<file_name>=g:<growsize>

If bigger than current file size, this will directly impact maximum file size.

Examples:
`data1.out=g:4096`
`data2.out=p:data_files,g:1024`
`data3.out=r:0x100000`
`data4.out=p:ota_1,g:max`

# Tips and tricks

* If You want to provide a read-only file system, e.g. for a web server, it makes sense to set default grow to zero, to save flash space. You can set then specific grow size for files You expect to change, e.g. configuration files.
* If You declare OTA partitions and would like to access them thru the virtual file system, create empty files (e.g. `touch ota-file-1.bin`) and in advanced options bind this file to the OTA partition, e.g. ota-file-1.bin=p:ota_0,g:max

# Compiling your project

Use `make all` as usual.

# Flashing your project

Use `make flash` as usual. Then run `make ffs-flash` which will flash the files.

# WARNING

File position and size conflicts internally and within the wider flash memory are NOT (yet) checked.