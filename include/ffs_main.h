#define FFS_ESP_FLASH_WRITE_BOUNDARY (SPI_FLASH_SEC_SIZE)
#define FFS_DIR_MAGIC_NUMBER (0x1234bcda)
#define FFS_DIR_INODE_PREFIX 0x2000
#define FFS_FILE_INODE_PREFIX 0x1000

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

typedef struct {
	uint16_t dd_vfs_idx; /*!< VFS index, not to be used by applications */
	uint16_t dd_rsv;     /*!< field reserved for future extension */
	uint32_t magic_number;
	char * dirname_ptr;
	char * dirname_end_ptr;
	ffs_file_index pidx;
	struct dirent data;
} ffs_DIR;

struct ffs_file_meta {
	char * name;
	unsigned int init_length;
	unsigned int max_length;
	const char * plabel;
	esp_partition_type_t ptype;
	esp_partition_subtype_t pstype;
	size_t offset;
};

struct ffs_open_file {
	ffs_file_index pidx;
	int flags;
	size_t position;
	void * mmap_ptr;
	esp_partition_t * partition_ptr;
	spi_flash_mmap_handle_t mmap_handle;
};

struct ffs_file_meta * ffs_get_file_meta(const char * name,
        size_t xstrlen, bool seek_directory, ffs_file_index startidx);
size_t ffs_interface_write(int fd, const void * data, size_t size);
int ffs_interface_open(const char * path, int flags, int mode);
int ffs_interface_fstat(int fd, struct stat * st);
int ffs_interface_close(int fd);
ssize_t ffs_interface_read(int fd, void * dst, size_t size);
off_t ffs_interface_lseek(int fd, off_t size, int mode);
int ffs_interface_stat(const char * path, struct stat * st);

DIR* ffs_interface_opendir(const char* name);
struct dirent* ffs_interface_readdir(DIR* pdir);
int ffs_interface_readdir_r(DIR* pdir, struct dirent* entry, struct dirent** out_dirent);
long ffs_interface_telldir(DIR* pdir);
void ffs_interface_seekdir(DIR* pdir, long offset);
int ffs_interface_closedir(DIR* pdir);
