#include <cinttypes>
#include <sys/stat.h>

typedef uint64_t fuse_ino_t;

struct fuse_req {
    struct fuse_session *se;
    uint64_t unique;
};
typedef struct fuse_req *fuse_req_t;

struct fuse_entry_param {
    /** Unique inode number
     *
     * In lookup, zero means negative entry (from version 2.5)
     * Returning ENOENT also means negative entry, but by setting zero
     * ino the kernel may cache negative entries for entry_timeout
     * seconds.
     */
    fuse_ino_t ino;

    /** Generation number for this entry.
     *
     * If the file system will be exported over NFS, the
     * ino/generation pairs need to be unique over the file
     * system's lifetime (rather than just the mount time). So if
     * the file system reuses an inode after it has been deleted,
     * it must assign a new, previously unused generation number
     * to the inode at the same time.
     *
     */
    uint64_t generation;

    /** Inode attributes.
     *
     * Even if attr_timeout == 0, attr must be correct. For example,
     * for open(), FUSE uses attr.st_size from lookup() to determine
     * how many bytes to request. If this value is not correct,
     * incorrect data will be returned.
     */
    struct stat attr;

    /** Validity timeout (in seconds) for inode attributes. If
        attributes only change as a result of requests that come
        through the kernel, this should be set to a very large
        value. */
    double attr_timeout;

    /** Validity timeout (in seconds) for the name. If directory
        entries are changed/deleted only as a result of requests
        that come through the kernel, this should be set to a very
        large value. */
    double entry_timeout;
    uint64_t        backing_action;
    uint64_t        backing_fd;
    uint64_t        bpf_action;
    uint64_t        bpf_fd;
};
