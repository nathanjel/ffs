#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "fcntl.h"
#include "esp_vfs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_spi_flash.h"
#include "sys/errno.h"
#include "sys/lock.h"
#include "soc/uart_struct.h"
#include "sdkconfig.h"

#ifdef CONFIG_FFS_ENABLED

#include "ffs_files.h"
#include "ffs_main.h"

#define FILE_CHECK_IF_SPI_OK(ret) \
	if (r != ESP_OK) {\
		errno = EIO;\
		free(tmp);\
		FILE_METHOD_RETURN(-1, write, W);\
	}

#define FILE_METHOD_START(LOG) \
	ESP_LOGD("FFS", "Entering (fd %d) " #LOG, fd); \
	xSemaphoreTake(File_IO_Lock, portMAX_DELAY); \
	if ((fd > 0) && (fd < ffs_end_of_list) \
		&& (mptrs[fd].base_ptr != NULL)) {

#define FILE_METHOD_STOP_RETURN(ret, LOG) \
		ESP_LOGD("FFS", "Finishing (fd %d) " #LOG, fd); \
		xSemaphoreGive(File_IO_Lock); \
		return ret; \
	} else { \
		ESP_LOGW("FFS", "Invalid call (fd %d) " #LOG, fd); \
		xSemaphoreGive(File_IO_Lock); \
		return EBADF; \
	}

#define FILE_METHOD_RETURN(value,LOG,LOGCAT) \
	ESP_LOG##LOGCAT("FFS", "Returning (fd %d, value %d, errno %d) " #LOG, fd, value, errno); \
	xSemaphoreGive(File_IO_Lock); \
	return value;

struct ffs_file_meta ffs_file_metadata[] = {
	FFS_FILE_METADATA
	{ NULL, 0, 0, 0, 0, 0 }
};

struct ffs_file_meta * ffs_get_file_meta(const char * name) {
	struct ffs_file_meta * ptr = ffs_file_metadata;
	while (ptr->name != NULL) {
		if (strcmp(ptr->name, name) == 0) {
			return ptr;
		}
		ptr++;
	}
	return NULL;
}

SemaphoreHandle_t File_IO_Lock;

struct ffs_open_file mptrs[ffs_end_of_list];

size_t ffs_interface_write(int fd, const void * data, size_t size) {
	size_t bytes_to_write = 0;
	FILE_METHOD_START(write)
	if (!(mptrs[fd].flags & FWRITE)) {
		errno = EACCES;
		FILE_METHOD_RETURN(-1, write, W)
	}
	char * tmp = malloc(FFS_ESP_FLASH_WRITE_BOUNDARY);
	if (!tmp) {
		errno = ENOMEM;
		FILE_METHOD_RETURN(-1, write, E)
	}
	size_t space_left_for_file = ffs_file_metadata[fd].max_length
	                             - mptrs[fd].position;
	size_t file_start_address_in_flash
	    = ffs_file_metadata[fd].flash_base_address + mptrs[fd].position;
	size_t file_start_address_page_start =
	    file_start_address_in_flash & ~(FFS_ESP_FLASH_WRITE_BOUNDARY - 1);
	size_t file_start_address_within_page =
	    file_start_address_in_flash & (FFS_ESP_FLASH_WRITE_BOUNDARY - 1);
	size_t bytes_within_page =
	    file_start_address_page_start + FFS_ESP_FLASH_WRITE_BOUNDARY
	    - file_start_address_in_flash;
	bytes_to_write = min(space_left_for_file, min(size, bytes_within_page));
	// read
	esp_err_t r = spi_flash_read(file_start_address_page_start, tmp, FFS_ESP_FLASH_WRITE_BOUNDARY);
	FILE_CHECK_IF_SPI_OK(r)
	// compare
	if (memcmp(data, tmp + file_start_address_within_page, bytes_to_write) == 0) {
		// what we want to write is already there so let's say we did
		mptrs[fd].position += bytes_to_write;
		free(tmp);
		FILE_METHOD_RETURN(bytes_to_write, write, D)
	}
	// copy
	memcpy(tmp + file_start_address_within_page, data, bytes_to_write);
	// erase
	r = spi_flash_erase_range(file_start_address_page_start, FFS_ESP_FLASH_WRITE_BOUNDARY);
	FILE_CHECK_IF_SPI_OK(r)
	// write
	r = spi_flash_write(file_start_address_page_start, tmp, FFS_ESP_FLASH_WRITE_BOUNDARY);
	FILE_CHECK_IF_SPI_OK(r)
	// release
	mptrs[fd].position += bytes_to_write;
	free(tmp);
	FILE_METHOD_STOP_RETURN(bytes_to_write, write)
}

int ffs_interface_open(const char * path, int flags, int mode) {
	int fd = 0;
	ESP_LOGD("FFS", "Entering open %s, flags:0x%x mode:\\0%o", path, flags, mode);
	xSemaphoreTake(File_IO_Lock, portMAX_DELAY);
	struct ffs_file_meta * ref = ffs_get_file_meta(path);
	if (ref == NULL) {
		errno = ENOENT;
		FILE_METHOD_RETURN(-1, open, W)
	}
	fd = ref->index;
	if (mptrs[ref->index].base_ptr != NULL)  {
		errno = EBUSY;
		FILE_METHOD_RETURN(-1, open, W)
	}
	// calculate map address
	size_t align_start_address = ref->flash_base_address & ~(FFS_ESP_MAP_BOUNDARY - 1);
	size_t align_start_offset = ref->offset + (ref->flash_base_address - align_start_address);
	size_t align_length = ((align_start_offset + ref->max_length) | (FFS_ESP_MAP_BOUNDARY - 1)) + 1;
	// open ;)
	esp_err_t res = spi_flash_mmap(
	                    align_start_address,
	                    align_length,
	                    SPI_FLASH_MMAP_DATA,
	                    (void*) & (mptrs[ref->index].base_ptr),
	                    &(mptrs[ref->index].mmap_handle)
	                );
	if (res != ESP_OK) {
		// no mmap, will resort to normal reads
		mptrs[ref->index].base_ptr = (void*)0xffffffff;
	} else {
		// set the starting address right
		mptrs[ref->index].base_ptr = mptrs[ref->index].base_ptr + align_start_offset;
	}
	mptrs[ref->index].position = 0;
	mptrs[ref->index].flags = flags + 1;	// to get FREAD/FWRITE
	// and let it go :)
	FILE_METHOD_RETURN(ref->index, open, D)
}

int ffs_interface_fstat(int fd, struct stat * st) {
	FILE_METHOD_START(fstat)
	memset(st, 0, sizeof(struct stat));
	st->st_ino = fd;
	st->st_mode = 0x666;
	st->st_size = ffs_file_metadata[fd].max_length;
	FILE_METHOD_STOP_RETURN(0, fstat)
}

int ffs_interface_close(int fd) {
	FILE_METHOD_START(close)
	mptrs[fd].base_ptr = NULL;
	mptrs[fd].flags = 0;
	spi_flash_munmap(mptrs[fd].mmap_handle);
	FILE_METHOD_STOP_RETURN(0, close)
}

ssize_t ffs_interface_read(int fd, void * dst, size_t size) {
	size_t maxread = size;
	FILE_METHOD_START(read)
	if (!(mptrs[fd].flags & FREAD)) {
		errno = EACCES;
		FILE_METHOD_RETURN(-1, read, W)
	}
	if (mptrs[fd].position + maxread > ffs_file_metadata[fd].max_length) {
		maxread = ffs_file_metadata[fd].max_length - mptrs[fd].position;
	}
	if (maxread) {
		if (mptrs[fd].base_ptr == (void*)0xffffffff) {
			spi_flash_read(ffs_file_metadata[fd].flash_base_address + mptrs[fd].position,
				dst, maxread);
		} else {
			memcpy(dst, mptrs[fd].base_ptr + mptrs[fd].position, maxread);
		}
		mptrs[fd].position += maxread;
	}
	FILE_METHOD_STOP_RETURN(maxread, read)
}

off_t ffs_interface_lseek(int fd, off_t size, int mode) {
	FILE_METHOD_START(lseek)
	off_t newpos = mptrs[fd].position;
	switch (mode) {
	case SEEK_SET:
		newpos = size;
		break;
	case SEEK_CUR:
		newpos += size;
		break;
	case SEEK_END:
		newpos = ffs_file_metadata[fd].max_length + size;
		errno = ESPIPE;
		FILE_METHOD_RETURN(-1, lseek, W)
		break;
	default:
		errno = EINVAL;
		FILE_METHOD_RETURN(-1, lseek, W)
	}
	if (newpos < 0 || newpos > ffs_file_metadata[fd].max_length) {
		errno = ESPIPE;
		FILE_METHOD_RETURN(-1, lseek, W)
	} else {
		mptrs[fd].position = newpos;
	}
	FILE_METHOD_STOP_RETURN(mptrs[fd].position, lseek)
}

int ffs_interface_stat(const char * path, struct stat * st) {
	int fd = 0;
	ESP_LOGD("FFS", "Entering stat %s", path);
	xSemaphoreTake(File_IO_Lock, portMAX_DELAY);
	struct ffs_file_meta * ref = ffs_get_file_meta(path);
	if (ref == NULL) {
		errno = ENOENT;
		FILE_METHOD_RETURN(-1, stat, W)
	}
	fd = ref->index;
	memset(st, 0, sizeof(struct stat));
	st->st_ino = ref->index;
	st->st_mode = ref->init_length;
	st->st_size = ref->max_length;
	FILE_METHOD_RETURN(0, stat, D)
}

esp_vfs_t ffs_vfs = {
	.fd_offset = 0,
	.flags = ESP_VFS_FLAG_DEFAULT,
	.write = &ffs_interface_write,
	.open = &ffs_interface_open,
	.fstat = &ffs_interface_fstat,
	.close = &ffs_interface_close,
	.read = &ffs_interface_read,
	.lseek = &ffs_interface_lseek,
	.stat = &ffs_interface_stat,
	.link = NULL,
	.unlink = NULL,
	.rename = NULL
};

void ffs_initialize() {
	File_IO_Lock = xSemaphoreCreateMutex();
	memset(mptrs, 0, sizeof (struct ffs_open_file) * ffs_end_of_list);
	ESP_ERROR_CHECK(esp_vfs_register(CONFIG_FFS_MOUNT_POINT, &ffs_vfs, NULL));
}

const char * ffs_mmap(void *addr,
                      size_t len,
                      int prot,
                      int flags,
                      int fd,
                      off_t offset) {
	ESP_LOGD("FFS", "Entering (fd %d) mmap", fd);
	xSemaphoreTake(File_IO_Lock, portMAX_DELAY);
	if ((fd > 0) && (fd < ffs_end_of_list)
	        && (mptrs[fd].base_ptr != NULL)) {
		if (offset > 0) {
			errno = EINVAL;
			FILE_METHOD_RETURN(0, mmap, W)
		}
		if (len > ffs_file_metadata[fd].max_length) {
			errno = EINVAL;
			FILE_METHOD_RETURN(0, mmap, W)
		}
		ESP_LOGD("FFS", "Finishing (fd %d) mmap", fd);
		xSemaphoreGive(File_IO_Lock);
		return mptrs[fd].base_ptr;
	} else {
		ESP_LOGW("FFS", "Invalid call (fd %d) mmap", fd);
		xSemaphoreGive(File_IO_Lock);
		errno = EBADF;
		return NULL;
	}
}

const char * ffs_munmap(void *addr, size_t len) {
	return NULL;
}

#else

void ffs_initialize() {}

const char * ffs_munmap(void *addr, size_t len) {
	return NULL;
}

const char * ffs_mmap(void *addr,
                      size_t len,
                      int prot,
                      int flags,
                      int fd,
                      off_t offset) {
	return NULL;
}

#endif