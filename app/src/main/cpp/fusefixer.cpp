#include <string>
#include <string_view>

#include <unicode/utf8.h>

#include <link.h>
#include <unistd.h>
#include <sys/mman.h>
#include <elf.h>

#include "lsp_api.h"
#include "elf_parser.hpp"
#include "maps_scan.hpp"
#include "logging.h"

#include "fuse.h"

// https://cs.android.com/android/platform/superproject/main/+/main:bionic/libc/upstream-openbsd/lib/libc/string/strcasecmp.c;l=35-88;drc=b364683ea65e24070c77f1673a8c155665eb894e

/*
 * This array is designed for mapping upper and lower case letter
 * together for a case independent comparison.  The mappings are
 * based upon ascii character sequences.
 */
static const char charmap[] = {
    '\000', '\001', '\002', '\003', '\004', '\005', '\006', '\007',
    '\010', '\011', '\012', '\013', '\014', '\015', '\016', '\017',
    '\020', '\021', '\022', '\023', '\024', '\025', '\026', '\027',
    '\030', '\031', '\032', '\033', '\034', '\035', '\036', '\037',
    '\040', '\041', '\042', '\043', '\044', '\045', '\046', '\047',
    '\050', '\051', '\052', '\053', '\054', '\055', '\056', '\057',
    '\060', '\061', '\062', '\063', '\064', '\065', '\066', '\067',
    '\070', '\071', '\072', '\073', '\074', '\075', '\076', '\077',
    '\100', '\141', '\142', '\143', '\144', '\145', '\146', '\147',
    '\150', '\151', '\152', '\153', '\154', '\155', '\156', '\157',
    '\160', '\161', '\162', '\163', '\164', '\165', '\166', '\167',
    '\170', '\171', '\172', '\133', '\134', '\135', '\136', '\137',
    '\140', '\141', '\142', '\143', '\144', '\145', '\146', '\147',
    '\150', '\151', '\152', '\153', '\154', '\155', '\156', '\157',
    '\160', '\161', '\162', '\163', '\164', '\165', '\166', '\167',
    '\170', '\171', '\172', '\173', '\174', '\175', '\176', '\177',
    '\200', '\201', '\202', '\203', '\204', '\205', '\206', '\207',
    '\210', '\211', '\212', '\213', '\214', '\215', '\216', '\217',
    '\220', '\221', '\222', '\223', '\224', '\225', '\226', '\227',
    '\230', '\231', '\232', '\233', '\234', '\235', '\236', '\237',
    '\240', '\241', '\242', '\243', '\244', '\245', '\246', '\247',
    '\250', '\251', '\252', '\253', '\254', '\255', '\256', '\257',
    '\260', '\261', '\262', '\263', '\264', '\265', '\266', '\267',
    '\270', '\271', '\272', '\273', '\274', '\275', '\276', '\277',
    '\300', '\301', '\302', '\303', '\304', '\305', '\306', '\307',
    '\310', '\311', '\312', '\313', '\314', '\315', '\316', '\317',
    '\320', '\321', '\322', '\323', '\324', '\325', '\326', '\327',
    '\330', '\331', '\332', '\333', '\334', '\335', '\336', '\337',
    '\340', '\341', '\342', '\343', '\344', '\345', '\346', '\347',
    '\350', '\351', '\352', '\353', '\354', '\355', '\356', '\357',
    '\360', '\361', '\362', '\363', '\364', '\365', '\366', '\367',
    '\370', '\371', '\372', '\373', '\374', '\375', '\376', '\377',
};

std::string escape_string(std::string_view sv) {
    std::string os;
    char buf[8];
    for (char i : sv) {
        if (i >= 32 && i < 127) [[likely]] {
            os += i;
        } else {
            snprintf(buf, sizeof(buf), "\\x%02x", (unsigned char) i);
            os += buf;
        }
    }
    return os;
}

// https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/fs/unicode/mkutf8data.c;l=2233;drc=231825b2e1ff6ba799c5eaf396d3ab2354e37c6b
// https://www.unicode.org/Public/12.1.0/ucd/DerivedCoreProperties.txt
// Default_Ignorable_Code_Point
inline bool is_default_ignorable_code_point(UChar32 ch) {
    return  ch == 0x00AD ||
            ch == 0x034F ||
            ch == 0x061C ||
            (0x115F <= ch && ch <= 0x1160) ||
            (0x17B4 <= ch && ch <= 0x17B5) ||
            (0x180B <= ch && ch <= 0x180E) ||
            (0x200B <= ch && ch <= 0x200F) ||
            (0x202A <= ch && ch <= 0x202E) ||
            (0x2060 <= ch && ch <= 0x206F) ||
            ch == 0x3164 ||
            (0xFE00 <= ch && ch <= 0xFE0F) ||
            ch == 0xFEFF ||
            ch == 0xFFA0 ||
            (0xFFF0 <= ch && ch <= 0xFFF8) ||
            (0x1BCA0 <= ch && ch <= 0x1BCA3) ||
            (0x1D173 <= ch && ch <= 0x1D17A) ||
            (0xE0000 <= ch && ch <= 0xE0FFF);
}

void remove_default_ignorable_code_point(std::string& str);

int svcasecmp_fix(std::string_view sv1, std::string_view sv2) {
    const char *cm = charmap;
    auto s1 = sv1.data(), s2 = sv2.data();
    auto l1 = sv1.size(), l2 = sv2.size();
    size_t i1 = 0, i2 = 0, j1 = 0, j2 = 0;

    UChar32 ch;
    while (i1 < l1 && i2 < l2) {
        if (i1 == j1) {
            do {
                i1 = j1;
                if (j1 == l1) break;
                U8_NEXT(s1, j1, l1, ch);
                // We can't do any thing if we occurred invalid utf-8 char.
                // In this case, j will increment by 1, so just break
                if (ch < 0) [[unlikely]] {
                    LOGW("invalid char at %zu-%zu : %s", i1, j1, escape_string(sv1).c_str());
                    break;
                }
            } while (is_default_ignorable_code_point(ch));
        }

        if (i2 == j2) {
            do {
                i2 = j2;
                if (j2 == l2) break;
                U8_NEXT(s2, j2, l2, ch);
                if (ch < 0) [[unlikely]] {
                    LOGW("invalid char at %zu-%zu : %s", i2, j2, escape_string(sv2).c_str());
                    break;
                }
            } while (is_default_ignorable_code_point(ch));
        }

        if (cm[s1[i1]] != cm[s2[i2]]) {
            break;
        }
        ++i1;
        ++i2;
    }
    if (i1 < l1 && i1 == j1) {
        do {
            i1 = j1;
            if (j1 == l1) break;
            U8_NEXT(s1, j1, l1, ch);
            // We can't do any thing if we occurred invalid utf-8 char.
            // In this case, j will increment by 1, so just break
            if (ch < 0) [[unlikely]] {
                LOGW("invalid char at %zu-%zu : %s", i1, j1, escape_string(sv1).c_str());
                break;
            }
        } while (is_default_ignorable_code_point(ch));
    }

    if (i2 < l2 && i2 == j2) {
        do {
            i2 = j2;
            if (j2 == l2) break;
            U8_NEXT(s2, j2, l2, ch);
            if (ch < 0) [[unlikely]] {
                LOGW("invalid char at %zu-%zu : %s", i2, j2, escape_string(sv2).c_str());
                break;
            }
        } while (is_default_ignorable_code_point(ch));
    }

    int ret = ((u_char) cm[s1[i1]] - (u_char) cm[s2[i2]]);
#ifndef NDEBUG
    {
        std::string ss1{sv1}, ss2{sv2};
        remove_default_ignorable_code_point(ss1);
        remove_default_ignorable_code_point(ss2);
        int ret2 = strcasecmp(ss1.c_str(), ss2.c_str());
        if (ret2 != ret) {
            LOGE("!!! strcasecmp implementation error: compare %s : %s, ret = %d, real ret=%d",
                 escape_string(sv1).c_str(), escape_string(sv2).c_str(), ret, ret2);
            return ret2;
        }
    }
#endif
    return ret;
}

int strcasecmp_fix(const char *s1, const char *s2) {
    return svcasecmp_fix(std::string_view{s1, strlen(s1)}, std::string_view{s2, strlen(s2)});
}

bool has_default_ignorable_code_point(const std::string& str) {
    auto buf = str.data();
    auto len = str.size();
    auto* s = reinterpret_cast<const uint8_t*>(buf);
    size_t i = 0;
    UChar32 ch;
    while (i < len) {
        U8_NEXT(s, i, len, ch);
        if (ch >= 0 && is_default_ignorable_code_point(ch)) {
            return true;
        }
    }
    return false;
}

void remove_default_ignorable_code_point(std::string& str) {
    auto buf = str.data();
    auto len = str.size();
    auto* s = reinterpret_cast<uint8_t*>(buf);
    size_t i = 0, j = 0, prev;
    UChar32 ch;
    while (i < len) {
        prev = i;
        U8_NEXT(s, i, len, ch);
        if (ch < 0) {
            LOGW("invalid char at %zu-%zu : %s", prev, i, escape_string(str).c_str());
        } else if (!is_default_ignorable_code_point(ch)) {
            while (prev < i) {
                buf[j++] = (char) s[prev++];
            }
        }
    }
    str.resize(j);
}

static HookFunType hook_func = nullptr;

bool (*old_is_package_owned_path)(const std::string& path, const std::string& fuse_path);
bool my_is_package_owned_path(const std::string& path, const std::string& fuse_path) {
    if (has_default_ignorable_code_point(path)) [[unlikely]] {
        std::string new_path{path};
        remove_default_ignorable_code_point(new_path);
        auto r = old_is_package_owned_path(new_path, fuse_path);
        LOGD("fix is_package_owned_path: %s res=%d", escape_string(path).c_str(), r);
        return r;
    }
    return old_is_package_owned_path(path, fuse_path);
}

bool (*old_is_app_accessible_path)(struct fuse* fuse, const std::string& path, uid_t uid);
bool my_is_app_accessible_path(struct fuse* fuse, const std::string& path, uid_t uid) {
    if (has_default_ignorable_code_point(path)) [[unlikely]] {
        std::string new_path{path};
        remove_default_ignorable_code_point(new_path);
        auto r = old_is_app_accessible_path(fuse, new_path, uid);
        LOGD("fix is_app_accessible_path: %s uid=%d res=%d", escape_string(path).c_str(), uid, r);
        return r;
    }
    return old_is_app_accessible_path(fuse, path, uid);
}

// Magic is here?
// https://cs.android.com/android/platform/superproject/main/+/main:packages/providers/MediaProvider/jni/FuseDaemon.cpp;l=699-701;drc=61197364367c9e404c7da6900658f1b16c42d0da
bool (*old_is_bpf_backing_path)(const std::string& path);
bool my_is_bpf_backing_path(const std::string& path) {
    if (has_default_ignorable_code_point(path)) [[unlikely]] {
        std::string new_path{path};
        remove_default_ignorable_code_point(new_path);
        auto r = old_is_bpf_backing_path(new_path);
        LOGD("fix is_bpf_backing_path: %s res=%d", escape_string(path).c_str(), r);
        return r;
    }
    return old_is_bpf_backing_path(path);
}

// hook strcasecmp to solve mount point passthrough
// https://cs.android.com/android/platform/superproject/+/android-latest-release:packages/providers/MediaProvider/jni/node-inl.h;l=529-555;drc=b415bef4d9bdd8f506e89577dafdb9cda12bcd69
// only NodeCompare uses strcasecmp
int (*old_strcasecmp)(const char *s1, const char *s2);
int my_strcasecmp(const char *s1, const char *s2) {
    return strcasecmp_fix(s1, s2);
}

// https://cs.android.com/android/platform/superproject/main/+/main:packages/providers/MediaProvider/jni/FuseUtils.cpp;l=50-52;drc=61197364367c9e404c7da6900658f1b16c42d0da
// Fix containsMount
// https://cs.android.com/android/platform/superproject/main/+/main:system/libbase/strings.cpp;l=119;drc=61197364367c9e404c7da6900658f1b16c42d0da
bool (*old_EqualsIgnoreCase)(std::string_view lhs, std::string_view rhs);
bool my_EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    return svcasecmp_fix(lhs, rhs) == 0;
}

int (*old_fuse_lowlevel_notify_inval_entry)(void *se, uint64_t parent,
                                     const char *name, size_t namelen);
int my_fuse_lowlevel_notify_inval_entry(void *se, uint64_t parent,
                                     const char *name, size_t namelen) {
    auto ret = old_fuse_lowlevel_notify_inval_entry(se, parent, name, namelen);
    LOGI("notify_inval_entry: ino=0x%lx name=%s ret=%d", (unsigned long) parent, name, ret);
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
    LOGI("notify_inval_inode: ino=0x%lx name=%s ret=%d", (unsigned long) ino, escape_string(*name).c_str(), ret);
    return ret;
}


std::string (*node_BuildPath_ptr)(void* node);

std::string inodePath(uint64_t ino) {
    if (ino == 1) [[unlikely]] {
        return "(ROOT)";
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "(%p)", (void*) ino);
    if (node_BuildPath_ptr) [[likely]] {
        return buf + node_BuildPath_ptr(reinterpret_cast<void*>(ino));
    } else {
        return buf;
    }
}

void (*old_pf_lookup)(fuse_req_t req, uint64_t parent, const char* name);
void my_pf_lookup(fuse_req_t req, uint64_t parent, const char* name) {
    LOGI("lookup: req=%lu parent=%s name=%s", (unsigned long) req->unique, escape_string(inodePath(parent)).c_str(), escape_string(name).c_str());
    old_pf_lookup(req, parent, name);
}

int (*old_fuse_reply_entry)(fuse_req_t req, const struct fuse_entry_param* e);
int my_fuse_reply_entry(fuse_req_t req, const struct fuse_entry_param* e) {
    auto ret = old_fuse_reply_entry(req, e);
    LOGI("fuse_reply_entry: req=%lu ino=%s timeout=%.2le attr_timeout=%.2le bpf_fd=%lu bpf_action=%lu backing_action=%lu backing_fd=%lu ret=%d", (unsigned long) req->unique,
         escape_string(inodePath(e->ino)).c_str(), e->entry_timeout, e->attr_timeout, (unsigned long) e->bpf_fd, (unsigned long) e->bpf_action, (unsigned long) e->backing_action, (unsigned long) e->backing_fd, ret);
    return ret;
}

static void (*old_pf_lookup_postfilter)(fuse_req_t req, fuse_ino_t parent, uint32_t error_in,
                                    const char* name, struct fuse_entry_out* feo,
                                    struct fuse_entry_bpf_out* febo);

static void my_pf_lookup_postfilter(fuse_req_t req, fuse_ino_t parent, uint32_t error_in,
                                 const char* name, struct fuse_entry_out* feo,
                                 struct fuse_entry_bpf_out* febo) {
    LOGI("pf_lookup_postfilter parent=%s name=%s", escape_string(inodePath(parent)).c_str(), name);
    old_pf_lookup_postfilter(req, parent, error_in, name, feo, febo);
}

#ifdef NDEBUG
static constexpr bool kDebugFuse = false;
#else
static constexpr bool kDebugFuse = true;
#endif

void on_library_loaded(const char *name, void *handle) {
    static constexpr char kLibFuseJni[] = "libfuse_jni.so";
    LOGD("loaded: %s", name);
    if (std::string(name).ends_with(kLibFuseJni)) {
        LOGI("hooking libfuse_jni");
        uintptr_t base_addr = 0;
        size_t load_sz = 0;
        auto callback = [&](dl_phdr_info& info) -> bool {
            if (!info.dlpi_name) return false;
            std::string_view name{info.dlpi_name};
            if (!name.ends_with(kLibFuseJni)) return false;
            if (!info.dlpi_phdr) return false;
            auto phdrs = info.dlpi_phdr;
            uintptr_t vaddr_min = UINTPTR_MAX, vaddr_max = 0;
            for (auto i = 0; i < info.dlpi_phnum; i++) {
                auto &phdr = phdrs[i];
                if (phdr.p_type == PT_LOAD) {
                    if (vaddr_min > phdr.p_vaddr) vaddr_min = phdr.p_vaddr;
                    if (vaddr_max < phdr.p_vaddr) vaddr_max = phdr.p_vaddr;
                }
            }
            load_sz = vaddr_max - vaddr_min;
            base_addr = info.dlpi_addr + vaddr_min;
            return true;
        };
        using Callback = decltype(callback);
        dl_iterate_phdr([](dl_phdr_info* info, size_t, void* data) -> int {
            return reinterpret_cast<Callback*>(data)->operator()(*info) ? 1 : 0;
        }, &callback);

        LOGD("base %p load sz %zu", (void*) base_addr, load_sz);

        size_t off;
        std::string path{};

        uintptr_t exec_addr = 0, exec_sz = 0;
        int exec_perm = 0;

        maps_scan::MapInfo::ForEach([&](const maps_scan::MapInfo& info) -> bool {
            if (info.start == base_addr) {
                off = info.offset;
                path = info.path;
            }
            if (info.start >= base_addr && info.end < base_addr + load_sz && info.path == path && (info.perms & PROT_EXEC) != 0) {
                exec_addr = info.start;
                exec_perm = info.perms;
                exec_sz = info.end - info.start;
                // meet executable, we can return
                return false;
            }
            return true;
        });

        if (path.empty()) {
            LOGE("not found in maps");
            return;
        }

        LOGD("so path %s off %zu exec addr %p sz %zu", path.c_str(), off, (void*) exec_addr, exec_sz);

        // make executable area rwx to prevent their vma from splitting
        // this fixes unwind
        if (exec_addr) {
            if (mprotect((void *) exec_addr, exec_sz, exec_perm | PROT_WRITE)) {
                PLOGE("fix memory perm");
            }
        }

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

            if constexpr (kDebugFuse) {
                node_BuildPath_ptr = reinterpret_cast<decltype(node_BuildPath_ptr)>(elf.getSymbAddress(
                    "_ZNK13mediaprovider4fuse4node9BuildPathEv"));
                LOGI("BuildPath: %p", node_BuildPath_ptr);
            }

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

            if constexpr (kDebugFuse) {
                p = elf.getSymbAddress("_ZN13mediaprovider4fuseL9pf_lookupEP8fuse_reqmPKc");
                LOGI("pf_lookup: %p", p);
                r = hook_func(p, (void *) my_pf_lookup, (void **) &old_pf_lookup);
                if (r != 0) {
                    LOGE("hook pf_lookup failed: %d", r);
                } else {
                    LOGI("hook pf_lookup success");
                }

                p = elf.getSymbAddress("_ZN13mediaprovider4fuseL20pf_lookup_postfilterEP8fuse_reqmjPKcP14fuse_entry_outP18fuse_entry_bpf_out");
                LOGI("pf_lookup_postfilter: %p", p);
                r = hook_func(p, (void *) my_pf_lookup_postfilter, (void **) &old_pf_lookup_postfilter);
                if (r != 0) {
                    LOGE("hook pf_lookup_postfilter failed: %d", r);
                } else {
                    LOGI("hook pf_lookup_postfilter success");
                }
            }
        }

        if (exec_addr) {
            if (mprotect((void *) exec_addr, exec_sz, exec_perm)) {
                PLOGE("fix memory perm back");
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

            auto do_hook_plt = [&](const char* show_name, const char* sym, void* hook, void** old, const char* alt = nullptr) {
                auto addrs = elf.FindPltAddr(sym);
                if (addrs.empty() && alt) {
                    addrs = elf.FindPltAddr(alt);
                }
                if (addrs.empty()) {
                    LOGE("no %s found", show_name);
                }
                for (auto a: addrs) {
                    LOGD("hooking %s %p", show_name, (void*)a);
                    if (mprotect(PAGE_START(a), pgsz, PROT_READ | PROT_WRITE) < 0) {
                        PLOGE("mprotect");
                    } else {
                        LOGD("hooked %s %p", show_name, (void*) a);
                        *old = *(void**) a;
                        *(void**)a = hook;
                        __builtin___clear_cache(PAGE_START(a), PAGE_END(a));
                    }
                }
            };

            do_hook_plt("strcasecmp", "strcasecmp", (void*) my_strcasecmp, (void**) &old_strcasecmp);
            do_hook_plt("EqualsIgnoreCase", "_ZN7android4base16EqualsIgnoreCaseENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEES5_",
                        (void*) my_EqualsIgnoreCase, (void**) &old_EqualsIgnoreCase, "_ZN7android4base16EqualsIgnoreCaseENSt3__117basic_string_viewIcNS1_11char_traitsIcEEEES5_");

            if constexpr (kDebugFuse) {
                do_hook_plt("fuse_lowlevel_notify_inval_entry", "fuse_lowlevel_notify_inval_entry",
                            (void *) my_fuse_lowlevel_notify_inval_entry,
                            (void **) &old_fuse_lowlevel_notify_inval_entry);
                do_hook_plt("fuse_lowlevel_notify_inval_inode", "fuse_lowlevel_notify_inval_inode",
                            (void *) my_fuse_lowlevel_notify_inval_inode,
                            (void **) &old_fuse_lowlevel_notify_inval_inode);
                do_hook_plt("fuse_reply_entry", "fuse_reply_entry",
                            (void *) my_fuse_reply_entry, (void **) &old_fuse_reply_entry);
            }
        }

    }
}

extern "C" [[gnu::visibility("default")]] [[gnu::used]]
NativeOnModuleLoaded native_init(const NativeAPIEntries *entries) {
    LOGI("Loaded");
    hook_func = entries->hook_func;
    return on_library_loaded;
}
