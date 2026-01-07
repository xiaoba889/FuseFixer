#include <string>
#include <string_view>

#include <unicode/utf8.h>
#include <unicode/uchar.h>

#include <link.h>
#include <unistd.h>
#include <sys/mman.h>
#include <elf.h>
#include <sys/stat.h>

#include "lsp_api.h"
#include "elf_parser.hpp"
#include "maps_scan.hpp"
#include "logging.h"

#undef LOGD
#define LOGD(...) 0

bool remove_default_ignorable_code_point(std::string& str) {
    auto buf = str.data();
    auto len = str.size();
    auto* s = reinterpret_cast<uint8_t*>(buf);
    int32_t i = 0, j = 0, prev;
    UChar32 ch;
    while (i < len) {
        prev = i;
        U8_NEXT(s, i, len, ch);
        if (ch < 0) {
            LOGE("error occurred while processing str: i=%d len=%zu", i, len);
            break;
        }
        if (!u_hasBinaryProperty(ch, UCHAR_DEFAULT_IGNORABLE_CODE_POINT)) {
            while (prev < i) {
                buf[j++] = (char) s[prev++];
            }
        }
    }
    str.resize(j);
    if (j != len) {
        LOGD("removed something in str %s", str.c_str());
        return true;
    }
    return false;
}

static HookFunType hook_func = nullptr;

bool (*old_is_package_owned_path)(const std::string& path, const std::string& fuse_path);
bool my_is_package_owned_path(const std::string& path, const std::string& fuse_path) {
    std::string new_path = path;
    remove_default_ignorable_code_point(new_path);
    auto r = old_is_package_owned_path(new_path, fuse_path);
    LOGD("is_package_owned_path: %s res=%d", path.c_str(), r);
    return r;
}

bool (*old_is_app_accessible_path)(struct fuse* fuse, const std::string& path, uid_t uid);
bool my_is_app_accessible_path(struct fuse* fuse, const std::string& path, uid_t uid) {
    std::string new_path = path;
    remove_default_ignorable_code_point(new_path);
    auto r = old_is_app_accessible_path(fuse, new_path, uid);
    LOGD("is_app_accessible_path: %s uid=%d res=%d", path.c_str(), uid, r);
    return r;
}

// Magic is here?
// https://cs.android.com/android/platform/superproject/main/+/main:packages/providers/MediaProvider/jni/FuseDaemon.cpp;l=699-701;drc=61197364367c9e404c7da6900658f1b16c42d0da
bool (*old_is_bpf_backing_path)(const std::string& path);
bool my_is_bpf_backing_path(const std::string& path) {
    std::string new_path = path;
    remove_default_ignorable_code_point(new_path);
    auto r = old_is_bpf_backing_path(new_path);
    LOGD("is_bpf_backing_path: %s res=%d", path.c_str(), r);
    return r;
}

// hook strcasecmp to solve mount point passthrough
// https://cs.android.com/android/platform/superproject/+/android-latest-release:packages/providers/MediaProvider/jni/node-inl.h;l=529-555;drc=b415bef4d9bdd8f506e89577dafdb9cda12bcd69
// only NodeCompare uses strcasecmp
// TODO: implement it without allocating memory
int (*old_strcasecmp)(const char *s1, const char *s2);
int my_strcasecmp(const char *s1, const char *s2) {
    std::string new_s1 = s1, new_s2 = s2;
    remove_default_ignorable_code_point(new_s1);
    remove_default_ignorable_code_point(new_s2);
    return old_strcasecmp(new_s1.c_str(), new_s2.c_str());
}

// https://cs.android.com/android/platform/superproject/main/+/main:packages/providers/MediaProvider/jni/FuseUtils.cpp;l=50-52;drc=61197364367c9e404c7da6900658f1b16c42d0da
// Fix containsMount
// https://cs.android.com/android/platform/superproject/main/+/main:system/libbase/strings.cpp;l=119;drc=61197364367c9e404c7da6900658f1b16c42d0da
bool (*old_EqualsIgnoreCase)(std::string_view lhs, std::string_view rhs);
bool my_EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    std::string new_lhs{lhs};
    if (remove_default_ignorable_code_point(new_lhs)) {
        LOGI("EqualsIgnoreCase fixed %s", new_lhs.c_str());
    }
    return old_EqualsIgnoreCase(new_lhs, rhs);
}

int (*old_fuse_lowlevel_notify_inval_entry)(void *se, uint64_t parent,
                                     const char *name, size_t namelen);
int my_fuse_lowlevel_notify_inval_entry(void *se, uint64_t parent,
                                     const char *name, size_t namelen) {
    auto ret = old_fuse_lowlevel_notify_inval_entry(se, parent, name, namelen);
    LOGI("notify_inval_entry: ino=0x%lx name=%s ret=%d", parent, name, ret);
    return ret;
}

int (*old_fuse_lowlevel_notify_inval_inode)(void *se, uint64_t ino,
                                     off_t off, off_t len);
int my_fuse_lowlevel_notify_inval_inode(void *se, uint64_t ino,
                                     off_t off, off_t len) {
    auto ret = old_fuse_lowlevel_notify_inval_inode(se, ino, off, len);
    std::string* name;
    if (ino == 1) {
        static std::string ROOT_NAME = "(ROOT)";
        name = &ROOT_NAME;
    } else {
        name = reinterpret_cast<std::string*>(ino);
    }
    LOGI("notify_inval_inode: ino=0x%lx name=%s ret=%d", ino, name->c_str(), ret);
    return ret;
}

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

std::string (*node_BuildPath_ptr)(void* node);

std::string inodePath(uint64_t ino) {
    if (ino == 1) [[unlikely]] {
        return "(ROOT)";
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "(%p)", ino);
    if (node_BuildPath_ptr) [[likely]] {
        return buf + node_BuildPath_ptr(reinterpret_cast<void*>(ino));
    } else {
        return buf;
    }
}

void (*old_pf_lookup)(fuse_req_t req, uint64_t parent, const char* name);
void my_pf_lookup(fuse_req_t req, uint64_t parent, const char* name) {
    LOGI("lookup: req=%lu parent=%s name=%s", req->unique, inodePath(parent).c_str(), name);
    old_pf_lookup(req, parent, name);
}

int (*old_fuse_reply_entry)(fuse_req_t req, const struct fuse_entry_param* e);
int my_fuse_reply_entry(fuse_req_t req, const struct fuse_entry_param* e) {
    auto ret = old_fuse_reply_entry(req, e);
    LOGI("fuse_reply_entry: req=%lu ino=%s timeout=%.2le attr_timeout=%.2le ret=%d", req->unique,
         inodePath(e->ino).c_str(), e->entry_timeout, e->attr_timeout, ret);
    return ret;
}

void on_library_loaded(const char *name, void *handle) {
    constexpr char kLibFuseJni[] = "libfuse_jni.so";
    LOGD("loaded: %s", name);
    if (std::string(name).ends_with(kLibFuseJni)) {
        LOGI("hooking libfuse_jni");
        uintptr_t base_addr = 0;
        auto callback = [&](dl_phdr_info& info) -> bool {
            if (!info.dlpi_name) return false;
            std::string_view name{info.dlpi_name};
            if (!name.ends_with(kLibFuseJni)) return false;
            if (!info.dlpi_phdr) return false;
            auto phdrs = info.dlpi_phdr;
            uintptr_t vaddr_min = UINTPTR_MAX;
            for (auto i = 0; i < info.dlpi_phnum; i++) {
                auto &phdr = phdrs[i];
                if (phdr.p_type == PT_LOAD) {
                    if (vaddr_min > phdr.p_vaddr) vaddr_min = phdr.p_vaddr;
                }
            }
            base_addr = info.dlpi_addr + vaddr_min;
            return true;
        };
        using Callback = decltype(callback);
        dl_iterate_phdr([](dl_phdr_info* info, size_t, void* data) -> int {
            return reinterpret_cast<Callback*>(data)->operator()(*info) ? 1 : 0;
        }, &callback);

        LOGD("base %p", (void*) base_addr);

        size_t off;
        std::string path{};

        maps_scan::MapInfo::ForEach([&](const maps_scan::MapInfo& info) -> bool {
            if (info.start == base_addr) {
                off = info.offset;
                path = info.path;
                return false;
            }
            return true;
        });

        if (path.empty()) {
            LOGE("not found in maps");
            return;
        }

        LOGD("so path %s off %zu", path.c_str(), off);

        {
            elf_parser::Elf elf{};
            if (!elf.InitFromFile(path, base_addr, true, off)) {
                LOGE("init elf failed");
                return;
            }

            constexpr char is_app_accessible_path[] =
                "_ZN13mediaprovider4fuseL22is_app_accessible_pathEP4fuseRKNSt6__ndk112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEj";
            constexpr char is_app_accessible_path_alt[] =
                "_ZN13mediaprovider4fuseL22is_app_accessible_pathEP4fuseRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEj";
            constexpr char is_package_owned_path[] =
                "_ZL21is_package_owned_pathRKNSt6__ndk112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEES7_";
            constexpr char is_package_owned_path_alt[] =
                "_ZL21is_package_owned_pathRKNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEES7_";
            constexpr char is_bpf_backing_path[] =
                "_ZL19is_bpf_backing_pathRKNSt6__ndk112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEE";
            constexpr char is_bpf_backing_path_alt[] =
                "_ZL19is_bpf_backing_pathRKNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEE";

            node_BuildPath_ptr = reinterpret_cast<decltype(node_BuildPath_ptr)>(elf.getSymbAddress("_ZNK13mediaprovider4fuse4node9BuildPathEv"));
            LOGI("BuildPath: %p", node_BuildPath_ptr);

            auto p = elf.getSymbAddress(is_app_accessible_path);
            if (!p) p = elf.getSymbAddress(is_app_accessible_path_alt);
            LOGD("is_app_accessible_path: %p", p);

            auto r = hook_func(p, (void *) my_is_app_accessible_path,
                               (void **) &old_is_app_accessible_path);
            if (r != 0) {
                LOGD("hook is_app_accessible_path failed: %d", r);
            }

            p = elf.getSymbAddress(is_package_owned_path);
            if (!p) p = elf.getSymbAddress(is_package_owned_path_alt);
            LOGD("is_package_owned_path: %p", p);
            r = hook_func(p, (void *) my_is_package_owned_path, (void **) &old_is_package_owned_path);
            if (r != 0) {
                LOGE("hook is_package_owned_path failed: %d", r);
            }

            p = elf.getSymbAddress(is_bpf_backing_path);
            if (!p) p = elf.getSymbAddress(is_bpf_backing_path_alt);
            LOGD("is_bpf_backing_path: %p", p);
            r = hook_func(p, (void *) my_is_bpf_backing_path, (void **) &old_is_bpf_backing_path);
            if (r != 0) {
                LOGE("hook is_bpf_backing_path failed: %d", r);
            }

            p = elf.getSymbAddress("_ZN13mediaprovider4fuseL9pf_lookupEP8fuse_reqmPKc");
            LOGI("pf_lookup: %p", p);
            r = hook_func(p, (void *) my_pf_lookup, (void **) &old_pf_lookup);
            if (r != 0) {
                LOGE("hook is_bpf_backing_path failed: %d", r);
            } else {
                LOGI("hook pf_lookup success");
            }
        }

        {
            elf_parser::Elf elf{};
            if (!elf.InitFromMemory((void*) base_addr, true)) {
                LOGE("init elf for dyn failed");
                return;
            }

            auto pgsz = getpagesize();
            auto mask = pgsz - 1;

#define PAGE_START(x) ((char*) ((x) & ~mask))
#define PAGE_END(x) PAGE_START(x + mask)

            auto do_hook_plt = [&](const char* sym, void* hook, void** old) {
                auto addrs = elf.FindPltAddr(sym);
                if (addrs.empty()) {
                    LOGE("no %s found", sym);
                }
                for (auto a: addrs) {
                    LOGI("hooking %s %p", sym, (void*)a);
                    if (mprotect(PAGE_START(a), pgsz, PROT_READ|PROT_WRITE) < 0) {
                        PLOGE("mprotect");
                    } else {
                        LOGI("hooked %s %p", sym, (void*) a);
                        *old = *(void**) a;
                        *(void**)a = hook;
                        __builtin___clear_cache(PAGE_START(a), PAGE_END(a));
                    }
                }
            };
            do_hook_plt("strcasecmp", (void*) my_strcasecmp, (void**) &old_strcasecmp);
            /*do_hook_plt("_ZN7android4base16EqualsIgnoreCaseENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEES5_",
                        (void*) my_EqualsIgnoreCase, (void**) old_EqualsIgnoreCase);*/
            do_hook_plt("fuse_lowlevel_notify_inval_entry",
                        (void*) my_fuse_lowlevel_notify_inval_entry, (void**) &old_fuse_lowlevel_notify_inval_entry);
            do_hook_plt("fuse_lowlevel_notify_inval_inode",
                        (void*) my_fuse_lowlevel_notify_inval_inode, (void**) &old_fuse_lowlevel_notify_inval_inode);
            do_hook_plt("fuse_reply_entry",
                        (void*) my_fuse_reply_entry, (void**) &old_fuse_reply_entry);
        }

    }
}

extern "C" [[gnu::visibility("default")]] [[gnu::used]]
NativeOnModuleLoaded native_init(const NativeAPIEntries *entries) {
    LOGI("Loaded");
    hook_func = entries->hook_func;
    return on_library_loaded;
}
