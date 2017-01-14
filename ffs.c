#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <dirent.h>
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "fcntl.h"
#include "esp_vfs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_spi_flash.h"
#include "esp_partition.h"
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

#define DIR_METHOD_INIT() if (1) {

#define DIR_METHOD_START(LOG) \
	ESP_LOGV("FFS", "Entering (DIR %p) " #LOG, pdir); \
	ffs_DIR * dir = (ffs_DIR*) pdir; \
	xSemaphoreTake(File_IO_Lock, portMAX_DELAY); \
	if (dir != NULL && (dir->magic_number == FFS_DIR_MAGIC_NUMBER)) {

#define DIR_METHOD_STOP_RETURN(ret, LOG) \
		ESP_LOGV("FFS", "Finishing (DIR %p) " #LOG, pdir); \
		xSemaphoreGive(File_IO_Lock); \
		return ret; \
	} else { \
		ESP_LOGW("FFS", "Invalid call (DIR %p) " #LOG, pdir); \
		xSemaphoreGive(File_IO_Lock); \
		errno = EBADF; \
		return -1; \
	}

#define DIR_METHOD_STOP_RETURN_PTR(ret, LOG) \
		ESP_LOGV("FFS", "Finishing (DIR %p) " #LOG, pdir); \
		xSemaphoreGive(File_IO_Lock); \
		return ret; \
	} else { \
		ESP_LOGW("FFS", "Invalid call (DIR %p) " #LOG, pdir); \
		xSemaphoreGive(File_IO_Lock); \
		return NULL; \
	}

#define DIR_METHOD_STOP_RETURN_NONE(LOG) \
		ESP_LOGV("FFS", "Finishing (DIR %p) " #LOG, pdir); \
		xSemaphoreGive(File_IO_Lock); \
		return; \
	} else { \
		ESP_LOGW("FFS", "Invalid call (DIR %p) " #LOG, pdir); \
		xSemaphoreGive(File_IO_Lock); \
		return; \
	}

#define DIR_METHOD_RETURN(value,LOG,LOGCAT) \
	ESP_LOG##LOGCAT("FFS", "Returning (DIR %p, value %d, errno %d) " #LOG, pdir, value, errno); \
	xSemaphoreGive(File_IO_Lock); \
	return value;

#define FILE_METHOD_START(LOG) \
	ESP_LOGV("FFS", "Entering (fd %d) " #LOG, fd); \
	xSemaphoreTake(File_IO_Lock, portMAX_DELAY); \
	if ((fd >= 0) && (fd < ffs_end_of_list) \
		&& (mptrs[fd].mmap_ptr != NULL)) {

#define FILE_METHOD_STOP_RETURN(ret, LOG) \
		ESP_LOGV("FFS", "Finishing (fd %d) " #LOG, fd); \
		xSemaphoreGive(File_IO_Lock); \
		return ret; \
	} else { \
		ESP_LOGW("FFS", "Invalid call (fd %d) " #LOG, fd); \
		xSemaphoreGive(File_IO_Lock); \
		errno = EBADF; \
		return -1; \
	}

#define FILE_METHOD_RETURN(value,LOG,LOGCAT) \
	ESP_LOG##LOGCAT("FFS", "Returning (fd %d, value %d, errno %d) " #LOG, fd, value, errno); \
	xSemaphoreGive(File_IO_Lock); \
	return value;

struct ffs_file_meta ffs_file_metadata[] = {
	FFS_FILE_METADATA
	{ NULL, 0, 0, 0, 0, 0, 0, 0 }
};

struct ffs_file_meta * ffs_get_file_meta(const char * name, size_t xstrlen, bool seek_directory, ffs_file_index startidx) {
	struct ffs_file_meta * ptr = ffs_file_metadata;
	if (startidx >= ffs_end_of_list)
		startidx = ffs_end_of_list;
	ptr += startidx;
	size_t search_len = xstrlen;
	if (name[search_len-1] == '/') {
		search_len--;
	}
	while (ptr->name != NULL) {
		if (seek_directory) {
			if ((search_len == 0) || ((strncmp(ptr->name, name, search_len) == 0)
			                          && ((*(ptr->name + search_len)) == '/'))) {
				return ptr;
			}
		} else {
			if (strncmp(ptr->name, name, xstrlen) == 0) {
				return ptr;
			}
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
	ESP_LOGV("FFS", "Write called with %p %u, file %d at position %u", data, size, fd, mptrs[fd].position);
	if (!(mptrs[fd].flags & FWRITE)) {
		errno = EACCES;
		FILE_METHOD_RETURN(-1, write, W)
	}
	char * tmp = NULL;
	tmp = malloc(FFS_ESP_FLASH_WRITE_BOUNDARY);
	if (!tmp) {
		errno = ENOMEM;
		FILE_METHOD_RETURN(-1, write, E)
	}
	size_t space_left_for_file = ffs_file_metadata[fd].max_length
	                             - mptrs[fd].position;
	size_t file_start_address_in_partition
	    = ffs_file_metadata[fd].offset + mptrs[fd].position;
	size_t file_start_address_page_start =
	    file_start_address_in_partition & ~(FFS_ESP_FLASH_WRITE_BOUNDARY - 1);
	size_t file_start_address_within_page =
	    file_start_address_in_partition & (FFS_ESP_FLASH_WRITE_BOUNDARY - 1);
	size_t bytes_within_page =
	    file_start_address_page_start + FFS_ESP_FLASH_WRITE_BOUNDARY
	    - file_start_address_in_partition;
	bytes_to_write = min(space_left_for_file, min(size, bytes_within_page));
	// read
	ESP_LOGV("FFS", "Reading %x bytes at %x%s%s",
	         FFS_ESP_FLASH_WRITE_BOUNDARY, file_start_address_page_start,
	         (mptrs[fd].partition_ptr == NULL) ? "" : " partition ",
	         ffs_file_metadata[fd].plabel);
	esp_err_t r;
	if (mptrs[fd].partition_ptr == NULL) {
		r = spi_flash_read(file_start_address_page_start, tmp, FFS_ESP_FLASH_WRITE_BOUNDARY);
	} else {
		r = esp_partition_read(mptrs[fd].partition_ptr, file_start_address_page_start,
		                       tmp, FFS_ESP_FLASH_WRITE_BOUNDARY);
	}
	FILE_CHECK_IF_SPI_OK(r)
	// compare
	ESP_LOGV("FFS", "Comparing %x bytes at %x", bytes_to_write, file_start_address_in_partition);
	if (memcmp(data, tmp + file_start_address_within_page, bytes_to_write) == 0) {
		// what we want to write is already there so let's say we did
		ESP_LOGV("FFS", "Data in Flash matches data in buffer");
		free(tmp);
		mptrs[fd].position += bytes_to_write;
		FILE_METHOD_RETURN(bytes_to_write, write, D)
	}
	// copy
	memcpy(tmp + file_start_address_within_page, data, bytes_to_write);
	// erase
	ESP_LOGV("FFS", "Erasing %x bytes at %x%s%s",
	         FFS_ESP_FLASH_WRITE_BOUNDARY, file_start_address_page_start,
	         (mptrs[fd].partition_ptr == NULL) ? "" : " partition ",
	         ffs_file_metadata[fd].plabel);
	if (mptrs[fd].partition_ptr == NULL) {
		r = spi_flash_erase_range(file_start_address_page_start, FFS_ESP_FLASH_WRITE_BOUNDARY);
	} else {
		r = esp_partition_erase_range(mptrs[fd].partition_ptr,
		                              file_start_address_page_start, FFS_ESP_FLASH_WRITE_BOUNDARY);
	}
	FILE_CHECK_IF_SPI_OK(r)
	// write
	ESP_LOGV("FFS", "Writing %x bytes at %x%s%s",
	         FFS_ESP_FLASH_WRITE_BOUNDARY, file_start_address_page_start,
	         (mptrs[fd].partition_ptr == NULL) ? "" : " partition ",
	         ffs_file_metadata[fd].plabel);
	if (mptrs[fd].partition_ptr == NULL) {
		r = spi_flash_write(file_start_address_page_start, tmp, FFS_ESP_FLASH_WRITE_BOUNDARY);
	} else {
		r = esp_partition_write(mptrs[fd].partition_ptr,
		                        file_start_address_page_start, tmp, FFS_ESP_FLASH_WRITE_BOUNDARY);
	}
	FILE_CHECK_IF_SPI_OK(r)
	// release
	free(tmp);
	mptrs[fd].position += bytes_to_write;
	FILE_METHOD_STOP_RETURN(bytes_to_write, write)
}

int ffs_interface_open(const char * path, int flags, int mode) {
	int fd = 0;
	ESP_LOGV("FFS", "Entering open %s, flags:0x%x mode:\\0%o", path, flags, mode);
	xSemaphoreTake(File_IO_Lock, portMAX_DELAY);
	struct ffs_file_meta * ref = ffs_get_file_meta(path, strlen(path), false, 0);
	if (ref == NULL) {
		errno = ENOENT;
		FILE_METHOD_RETURN(-1, open, W)
	}
	fd = ref->index;
	if (mptrs[ref->index].mmap_ptr != NULL)  {
		errno = EBUSY;
		FILE_METHOD_RETURN(-1, open, W)
	}
	// first establish the partition
	esp_err_t res = ESP_FAIL;
	if (ref->plabel != NULL) {	// do we have a partition defined?
		mptrs[ref->index].partition_ptr = (esp_partition_t *)
		                                  esp_partition_find_first(ref->ptype, ref->pstype, ref->plabel);
		if (mptrs[ref->index].partition_ptr == NULL) {
			errno = ENOENT;
			FILE_METHOD_RETURN(-1, open, W)
		}
		// open ;)
		res = esp_partition_mmap(
		          mptrs[ref->index].partition_ptr,
		          ref->offset,
		          ref->max_length,
		          SPI_FLASH_MMAP_DATA,
		          (const void **)(&(mptrs[ref->index].mmap_ptr)),
		          &(mptrs[ref->index].mmap_handle)
		      );
	}
	if (res != ESP_OK) {
		// no mmap, will resort to direct partition reads
		mptrs[ref->index].mmap_ptr = (void*)0xffffffff;
	}
	mptrs[ref->index].position = 0;
	mptrs[ref->index].flags = flags + 1;	// to get FREAD/FWRITE
	// and let it go :)
	FILE_METHOD_RETURN(ref->index, open, D)
}

static void ffs_setstat(int fd, struct stat * st) {
	st->st_ino = fd + FFS_FILE_INODE_PREFIX;
	st->st_mode = 0666;
	st->st_spare1 = ffs_file_metadata[fd].init_length;
	st->st_size = ffs_file_metadata[fd].max_length;
}

int ffs_interface_fstat(int fd, struct stat * st) {
	FILE_METHOD_START(fstat)
	memset(st, 0, sizeof(struct stat));
	ffs_setstat(fd, st);
	FILE_METHOD_STOP_RETURN(0, fstat)
}

int ffs_interface_close(int fd) {
	FILE_METHOD_START(close)
	mptrs[fd].partition_ptr = NULL;
	mptrs[fd].mmap_ptr = NULL;
	mptrs[fd].flags = 0;
	if (mptrs[fd].mmap_handle) {
		spi_flash_munmap(mptrs[fd].mmap_handle);
		mptrs[fd].mmap_handle = 0;
	}
	FILE_METHOD_STOP_RETURN(0, close)
}

ssize_t ffs_interface_read(int fd, void * dst, size_t size) {
	size_t maxread = size;
	FILE_METHOD_START(read)
	ESP_LOGV("FFS", "Read called with %p %u, file %d at position %u",
	         dst, size, fd, mptrs[fd].position);
	if (!(mptrs[fd].flags & FREAD)) {
		errno = EACCES;
		FILE_METHOD_RETURN(-1, read, W)
	}
	if (mptrs[fd].position + maxread > ffs_file_metadata[fd].max_length) {
		maxread = ffs_file_metadata[fd].max_length - mptrs[fd].position;
	}
	if (maxread) {
		if (mptrs[fd].mmap_ptr == (void*)0xffffffff) {	// no mmap
			if (mptrs[fd].partition_ptr == NULL) { // no partition - raw access
				spi_flash_read(ffs_file_metadata[fd].offset + mptrs[fd].position,
				               dst, maxread);
			} else { // access thru partition (allows encryption)
				esp_partition_read(
				    mptrs[fd].partition_ptr, ffs_file_metadata[fd].offset + mptrs[fd].position,
				    dst, maxread);
			}
		} else {
			memcpy(dst, mptrs[fd].mmap_ptr + mptrs[fd].position, maxread);
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
	ESP_LOGV("FFS", "Entering stat %s", path);
	xSemaphoreTake(File_IO_Lock, portMAX_DELAY);
	struct ffs_file_meta * ref = ffs_get_file_meta(path, strlen(path), false, 0);
	if (ref == NULL) {
		errno = ENOENT;
		FILE_METHOD_RETURN(-1, stat, W)
	}
	fd = ref->index;
	memset(st, 0, sizeof(struct stat));
	ffs_setstat(fd, st);
	FILE_METHOD_RETURN(0, stat, D)
}

DIR* ffs_interface_opendir(const char* name) {
	xSemaphoreTake(File_IO_Lock, portMAX_DELAY);
	ffs_DIR * pdir = NULL;
	if (name == NULL) {
		errno = ENOENT;
		DIR_METHOD_RETURN(0, opendir, E)
	}
	DIR_METHOD_INIT()
	struct ffs_file_meta * ptr = ffs_get_file_meta(name, strlen(name), true, 0);
	if (ptr == NULL) {
		errno = ENOENT;
		DIR_METHOD_RETURN(0, opendir, W)
	}
	pdir = malloc(sizeof(ffs_DIR));
	pdir->magic_number = FFS_DIR_MAGIC_NUMBER;
	pdir->dirname_ptr = ptr->name;
	pdir->dirname_end_ptr = ptr->name + strlen(name);
	if ((pdir->dirname_end_ptr > pdir->dirname_ptr)
	        && ((*(pdir->dirname_end_ptr - 1)) == '/')) {
		pdir->dirname_end_ptr--;
	}
	if ((*(pdir->dirname_end_ptr)) != '/') {
		errno = ENOTDIR;
		DIR_METHOD_RETURN(0, opendir, W)
	}
	pdir->pidx = 0;
	DIR_METHOD_STOP_RETURN_PTR((DIR*)pdir, opendir)
}

#define FFS_SET_DIRENT(de) \
	de.d_type = DT_REG; \
	de.d_ino = ptr->index + FFS_DIR_INODE_PREFIX; \
	strcpy(de.d_name, ptr->name + (dir->dirname_end_ptr - dir->dirname_ptr) + 1); \
	char * w = strchr(de.d_name, '/'); \
	if (w) { \
		(*w) = 0; \
		de.d_type = DT_DIR; \
	}

#define FFS_FIND_NEXT_ENTRY(de) \
	dir->pidx = ptr->index + 1; \
	if (de.d_type == DT_DIR) { \
		while ((dir->pidx < ffs_end_of_list) \
			&& (strncmp(ffs_file_metadata[dir->pidx].name + (dir->dirname_end_ptr - dir->dirname_ptr) + 1, de.d_name, w - de.d_name) == 0) \
			&& (*(ffs_file_metadata[dir->pidx].name + (dir->dirname_end_ptr - dir->dirname_ptr) + 1 + (w - de.d_name)) == '/')) { \
			dir->pidx++; \
		} \
	}

struct dirent* ffs_interface_readdir(DIR * pdir) {
	DIR_METHOD_START(readdir)
	struct ffs_file_meta * ptr = ffs_get_file_meta(dir->dirname_ptr,
	                             dir->dirname_end_ptr - dir->dirname_ptr, true, dir->pidx);
	if (ptr == NULL) {
		DIR_METHOD_RETURN(0, readdir, V);
	}
	FFS_SET_DIRENT(dir->data)
	FFS_FIND_NEXT_ENTRY(dir->data)
	DIR_METHOD_STOP_RETURN_PTR(&(dir->data), readdir)
}

int ffs_interface_readdir_r(DIR * pdir, struct dirent * entry, struct dirent** out_dirent) {
	DIR_METHOD_START(readdir_r)
	struct ffs_file_meta * ptr = ffs_get_file_meta(dir->dirname_ptr,
	                             dir->dirname_end_ptr - dir->dirname_ptr, true, dir->pidx);
	if (ptr == NULL) {
		(*out_dirent) = NULL;
		DIR_METHOD_RETURN(0, readdir, V);
	}
	(*out_dirent) = entry;
	FFS_SET_DIRENT((*entry))
	FFS_FIND_NEXT_ENTRY((*entry))
	DIR_METHOD_STOP_RETURN(0, readdir_r)
}

long ffs_interface_telldir(DIR * pdir) {
	DIR_METHOD_START(telldir)
	return dir->pidx;
	DIR_METHOD_STOP_RETURN(0, telldir)
}

void ffs_interface_seekdir(DIR * pdir, long offset) {
	DIR_METHOD_START(seekdir)
	dir->pidx += offset;
	if (dir->pidx >= ffs_end_of_list) {
		dir->pidx = ffs_end_of_list - 1;
	}
	DIR_METHOD_STOP_RETURN_NONE(seekdir)
}

int ffs_interface_closedir(DIR * pdir) {
	DIR_METHOD_START(closedir)
	free(pdir);
	DIR_METHOD_STOP_RETURN(0, closedir)
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
	.rename = NULL,
	.opendir = ffs_interface_opendir,
	.readdir = ffs_interface_readdir,
	.readdir_r = ffs_interface_readdir_r,
	.telldir = ffs_interface_telldir,
	.seekdir = ffs_interface_seekdir,
	.closedir = ffs_interface_closedir,
	.mkdir = NULL,
	.rmdir = NULL
};

void ffs_initialize() {
	File_IO_Lock = xSemaphoreCreateMutex();
	memset(mptrs, 0, sizeof (struct ffs_open_file) * ffs_end_of_list);
	ESP_ERROR_CHECK(esp_vfs_register(CONFIG_FFS_MOUNT_POINT, &ffs_vfs, NULL));
}

#else

void ffs_initialize() {}

#endif