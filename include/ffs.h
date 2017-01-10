void ffs_initialize();
const char * ffs_mmap(void *addr,
             size_t len,
             int prot,
             int flags,
             int fd,
             off_t offset);
const char * ffs_munmap(void *addr, size_t len);