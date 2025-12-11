#include <string>
#include <string_view>

#include <unicode/utf8.h>
#include <unicode/uchar.h>

#include <link.h>
#include <unistd.h>
#include <sys/mman.h>
#include <elf.h>

#include "lsp_api.h"
#include "elf_parser.hpp"
#include "maps_scan.hpp"
#include "logging.h"

void remove_default_ignorable_code_point(std::string& str) {
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
    }
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

void on_library_loaded(const char *name, void *handle) {
    constexpr char kLibFuseJni[] = "libfuse_jni.so";
    LOGD("loaded: %s", name);
    if (std::string(name).ends_with(kLibFuseJni)) {
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
                // "_ZN13mediaprovider4fuseL22is_app_accessible_pathEP4fuseRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEj"
                "_ZN13mediaprovider4fuseL22is_app_accessible_pathEP4fuseRKNSt6__ndk112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEj";
            constexpr char is_package_owned_path[] = //"_ZL21is_package_owned_pathRKNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEES7_"
                "_ZL21is_package_owned_pathRKNSt6__ndk112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEES7_";
            constexpr char is_bpf_backing_path[] = "_ZL19is_bpf_backing_pathRKNSt6__ndk112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEE";

            auto p = elf.getSymbAddress(is_app_accessible_path);
            LOGD("is_app_accessible_path: %p", p);

            auto r = hook_func(p, (void *) my_is_app_accessible_path,
                               (void **) &old_is_app_accessible_path);
            LOGD("hook is_app_accessible_path result %d", r);

            p = elf.getSymbAddress(is_package_owned_path);
            LOGD("is_package_owned_path: %p", p);
            hook_func(p, (void *) my_is_package_owned_path, (void **) &old_is_package_owned_path);
            LOGD("hook is_package_owned_path result %d", r);

            p = elf.getSymbAddress(is_bpf_backing_path);
            LOGD("is_bpf_backing_path: %p", p);
            hook_func(p, (void *) my_is_bpf_backing_path, (void **) &old_is_bpf_backing_path);
            LOGD("hook is_bpf_backing_path result %d", r);
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

            auto addrs = elf.FindPltAddr("strcasecmp");
            if (addrs.empty()) {
                LOGE("no strcasecmp found");
            }
            for (auto a: addrs) {
                LOGD("hooking strcasecmp %p", (void*)a);
                if (mprotect(PAGE_START(a), pgsz, PROT_READ|PROT_WRITE) < 0) {
                    PLOGE("mprotect");
                } else {
                    LOGD("hooked strcasecmp %p", (void*) a);
                    old_strcasecmp = *(decltype(old_strcasecmp)*) a;
                    *(void**)a = (void*) my_strcasecmp;
                    __builtin___clear_cache(PAGE_START(a), PAGE_END(a));
                }
            }
        }

    }
}

extern "C" [[gnu::visibility("default")]] [[gnu::used]]
NativeOnModuleLoaded native_init(const NativeAPIEntries *entries) {
    hook_func = entries->hook_func;
    return on_library_loaded;
}
