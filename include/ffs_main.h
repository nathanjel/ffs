#define FFS_ESP_MAP_BOUNDARY (0xffff + 1) // as hardcoded in spi_flash_mmap
#define FFS_ESP_FLASH_WRITE_BOUNDARY (SPI_FLASH_SEC_SIZE)

#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

typedef enum {
	FFS_FILE_LIST
	ffs_end_of_list
} ffs_file_index;

struct ffs_file_meta {
	char * name;
	ffs_file_index index;
	unsigned int init_length;
	unsigned int max_length;
	unsigned int flash_base_address;
	unsigned int offset;
};

struct ffs_open_file {
	void * base_ptr;
	size_t position;
	int flags;
	spi_flash_mmap_handle_t mmap_handle;
};

struct ffs_file_meta * ffs_get_file_meta(const char * name);
size_t ffs_interface_write(int fd, const void * data, size_t size);
int ffs_interface_open(const char * path, int flags, int mode);
int ffs_interface_fstat(int fd, struct stat * st);
int ffs_interface_close(int fd);
ssize_t ffs_interface_read(int fd, void * dst, size_t size);
off_t ffs_interface_lseek(int fd, off_t size, int mode);
int ffs_interface_stat(const char * path, struct stat * st);
