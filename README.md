# FuseFixer

通过 Xposed 模块 hook MediaProvider 中的 Fuse Daemon 解决存储目录中 Android/data 等目录泄露包名存在性的问题

当前实现对于 Fuse BPF 仍然存在[不可解决的问题](#修复-fuse-bpf-后仍然存在的问题)。

# 原理

## 简要介绍

Android 的外置存储，即 /storage/emulated 目录，一般使用内置存储的 /data/media 作为底层文件系统，通过 sdcardfs （已弃用）或者 FUSE 实现访问控制。而内置存储，即 data 分区 (/data 目录) 一般采用 f2fs 或者 ext4 文件系统，并且在 /data/media 开启了 casefold 以支持大小写不敏感的文件名。然而用户空间 Fuse Daemon 仅支持英文字母的大小写不敏感，而内核的 casefold 机制根据 Unicode 规范实现，不仅支持大小写不敏感，还会过滤默认可忽略码点，因此存在不一致的现象。通过 FUSE 访问时，可以向文件名插入默认可忽略码点，以绕过 Fuse Daemon 的一些检查机制（即 Fuse Daemon 认为这些目录不是 Android/data/pkg 等需要过滤的目录，而内核认为是），从而可以访问本来不该访问的路径（包括探测 Android/data/pkg 的可访问性来探测包的存在性）。尽管已经有多个补丁对 Fuse Daemon 进行加固，但仍然存在一些方法可以通过 Android/data 目录探测包的存在性。此外， Fuse BPF 的实现问题也是导致探测的原因。

详细的分析请见下文↓

## 内核 casefold

内核对开启了 casefold 的文件系统和目录，在执行文件名查找时，使用 utf8 正规化的 NFDICF 方式将其转化为唯一的表示形式。这就导致输入不同文件名可能映射到相同的文件名。

https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/fs/unicode/utf8-core.c;l=99;drc=47122afca6b6725e287a94fa56e1caf464b98e14

https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/include/linux/unicode.h;l=48;drc=549790b54108c2c643794e84e75fae10fd7e139e

```c
/*
 * Two normalization forms are supported:
 * 1) NFDI
 *   - Apply unicode normalization form NFD.
 *   - Remove any Default_Ignorable_Code_Point.
 * 2) NFDICF
 *   - Apply unicode normalization form NFD.
 *   - Remove any Default_Ignorable_Code_Point.
 *   - Apply a full casefold (C + F).
 */
enum utf8_normalization {
	UTF8_NFDI = 0,
	UTF8_NFDICF,
	UTF8_NMAX,
};
```

[generic_set_sb_d_ops](https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/fs/libfs.c;l=1973;drc=510fc2fc2d659d4883b80538f75a9f16f877ab5c)

根据 Unicode 规范，只有利用可忽略码点和使用不同大小写能够构造出正规化后为 Android 和 data 的字符串，其他正规化方式无法得出，因此我们只处理默认可忽略码点。

## 挂载点穿透问题

dentry 表示文件系统中的目录树结构，与挂载点无关
- d_parent 是指向父目录 dentry 的指针；
- d_name 表示目录名字；
- d_inode 即目录指向的文件。

mount / vfsmount 表示一个挂载点。其中 vfsmount 是被 mount 包含的结构，两者是一体的。
- mnt.mnt_sb 表示文件系统的超级块，多个挂载点可以指向同一个文件系统，一个文件系统可对应多个挂载点，bind mount 或者 unshare 都会复制挂载点；
- mnt.mnt_root 是指向该挂载点的根目录的 dentry 指针，这个根目录实际上不一定是文件系统的真正根目录，而可以是文件系统中的任意 dentry（bind mount 就可以让挂载点的根目录指向非真实根目录）；
- mnt_parent 表示父挂载点，是一个 mount 指针，而 mnt_mountpoint 即挂载点，是一个 dentry 指针，并且应该是来自 mnt_parent 的文件系统的 dentry 。也就是说，挂载点也是具有树结构的，且通过 dentry 进行连接。

path 是用户看到的路径，由两部分组成：mnt 和 dentry ，dentry 就是路径终端的目录入口，mnt 是查找这个目录入口的来源挂载点 。因此路径实际上既与文件系统相关，也与挂载点相关。file 对象会记录打开时候的 path ，procfs 又暴露了进程的 fd 对应 file 对象的 path ，因此可以通过 procfs 检查一个文件的 path ，进而了解 dentry 的内部状态

在路径查找时，如果一个 dentry 上有挂载点，需要转移到这个挂载点上，但是 dentry 对象不包含挂载点的信息，也不会记录上面有哪些挂载点。挂载点与 dentry 的对应关系实际上是由[挂载树的 hash 表记录的](https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/fs/namespace.c;l=795-803;drc=0a5ee589878c26cae74f0ed32486da458a1fe178)（这是一个 mount 结构体里面的成员，将挂载树上的所有挂载点通过 hash 表串联起来），从一个挂载点的文件系统的 dentry 转移到另一个挂载点上，需要遍历这个 hash 表，找到 mnt_parent 和 dentry 对应的那个挂载点。如此一来这个下层的 dentry 下的文件树就没法访问了，也就是被它的子挂载点「屏蔽」了

因此，如果如果一个文件系统的某个目录能够被两个不同的 dentry 指向，那么如果其中只有一个 dentry 下有一个 mount ，那么透过另一个 dentry 还是能访问到这个被挂载点「屏蔽」目录的内容

对于支持 casefold 的文件系统，假如等价的文件名对应到不同 dentry ，那么就会出现这样的问题。因此需要确保对等价的文件名返回相同的 dentry 对象。内核提供了 dentry_operations ，其中 d_hash 和 d_compare 可以用于计算特殊的文件系统上的名字的 hash 和比较名字，确保查找 dentry 对这些等价名字总返回同一个对象。内核的 libfs 还提供了 [generic_set_sb_d_ops](https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/fs/libfs.c;l=1973;drc=510fc2fc2d659d4883b80538f75a9f16f877ab5c) ，给支持 casefold 的文件系统提供了 d_hash 和 d_compare 的实现，目前 ext4 和 f2fs 都使用这一套 dentry operations 。

> 我们知道 dentry 是有自己的名字的，且这个名字当然是可以从用户空间获取的：通过 d_path 获取一个 path 的完整路径时就会显示路径上每一个 dentry 的 name ，而 linux 中常用的获取绝对路径方法是通过 readlink /proc/self/fd/N ，其中必然会使用这个函数。由于上述 d_op 的存在，多个名字对应了同一个 dentry ，那么 dentry 的名字是谁决定？  
> 如果先后打开 /data/media/0/Android 和 /data/media/0/aNdroid ，可以发现两者的完整路径都是 /data/media/0/Android  
> 但是如果在没有对应 dentry 缓存的情况下，实际上是使用先被 lookup 到的名字作为 dentry 的名字，这说明文件系统实现并没有用底层存储的文件名字作为 dentry 的名字，只是复用了首次 lookup 的缓存。
> 可以使用 `echo 2 > /proc/sys/vm/drop_caches` 清除 dentry 缓存，注意正在被使用的 dentry 是无法被清除的，因此最好找一个未被打开的 dentry 进行验证。

那么对于 FUSE 这样的文件系统，它没有使用 d_op ，如何确保上述等价文件名的 dentry 一致性？这实际上依赖于内核的一个机制，就是目录 inode 只能对应于一个 dentry ，如果对不同 dentry 的查询返回了相同 inode ，且这是一个目录 inode ，则内核认为发生了 rename ，并将已有的 dentry 改名成新的并返回，这实际上是处理网络文件系统这种重命名可能不发生在本地的情况的。因此如果 fuse 对等价文件名的目录返回相同的 inode ，那么内核会自动返回相同的 dentry 。这个过程发生在 [d_splice_alias](https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/fs/dcache.c;l=2994-3043;drc=0a5ee589878c26cae74f0ed32486da458a1fe178) 中，这个函数的作用是作为 inode_operations.lookup 的返回值，它将一个 inode 和一个 dentry 关联。

由于 Android 的 fuse 只将大小写不同的名字视作等价，对忽略大小写情况下相等的 lookup 请求返回相同的 inode ，因此即使大小写发生变化，仍然能对应到正确的 dentry ，从而不影响挂载点；然而对于可忽略码点没有返回相同 inode ，因此导致了「挂载点穿透」

从 Fuse Daemon 的设计可以看出，对 Android/data 的访问本不应经过 fuse （因为访问控制模型不同，外置存储的读写权限基于 gid ，而外置私有存储目录只有 app 自身能访问），实际上在未开启 Fuse BPF 的内核上，都需要 bind mount 下层文件系统（即 /data/media/USER）的 Android/data 到 /storage/emulated/USER/Android/data ，从而实现外置私有存储目录在 app 之间隔离，然而这导致包名泄露（因为 Android/data 目录必须对所有用户具有 x 权限，因此其下每个目录都可以用 stat 探测存在性），违反了包可见性，因此又推出了 vold app data dir isolation 这个方法，类似于 data dir isolation ，利用 tmpfs + bind mount 只挂载需要的存储目录来隐藏其他不可见的 app 的目录。

FuseFixer 的解决方案是 hook 用于比较目录的函数，过滤其输入中的默认可忽略码点，保证 Fuse Daemon 内部将具有可忽略码点的名字映射到正确的 inode 。

## FUSE BPF

Fuse BPF 看起来是用于取代 vold app data dir isolation 的一种方案，其让 Fuse Daemon 为 fuse inode 设置特定的 fuse bpf 参数，让某个 fuse inode 被 backing inode 接管，对 fuse inode 的文件系统调用透传给 backing inode 的下层文件系统，从而实现和 bind mount 类似的效果。

inode 被接管的情况下，一般不会再向 fuse daemon 发出这个 inode 相关的请求。但是在附加 backing inode 的同时也可以附加 bpf 程序，在进行下层文件系统的请求之前和之后都会执行 bpf 程序，而 bpf 程序可以决定是否将请求转发给 Fuse Daemon ，不过发起的不是通常的 fuse request ，而是具有 `_prefilter` 和 `_postfilter` 后缀的。Fuse Daemon 可以在这些请求中重新配置 backing inode 和 bpf prog 。

[fuseMedia bpf prog](https://cs.android.com/android/platform/superproject/main/+/main:system/bpfprogs/fuseMedia.c;l=2;drc=93c094d21aa1b7c01ba08804d090392ea07f420a)

fuse bpf 的内核部分代码在分支上，不在 mainline 。

https://cs.android.com/android/kernel/superproject/+/common-android16-6.12:common/fs/fuse/Kconfig;l=67-73;drc=8be90ef631b70dedc3c43b247aeae296a633dcb2

fuse bpf 的作用是让一个 fuse inode 与一个 backing inode 对应，所有的请求被转发到这个 backing inode 上，无需通过 fuse daemon ，并允许附加一个 bpf program 对 fuse 请求进行修改，或决定是否转发给 fuse daemon 处理。

Android Media Provider 的 fuse daemon 在设计上实际上就是希望 Android/data 完全透传到底层，从内部设计就能看出来，Fuse Daemon 完全不允许通过自身去访问 Android/data ，在没有 fuse bpf 的版本上是将 Android/data 整个 bind mount 过去，而 fuse bpf 的机制能够取代 bind mount 实现透传。

do_lookup 会调用 fuse_bpf_install 为 inode 配置 fuse bpf

```cpp
void fuse_bpf_install(struct fuse* fuse, struct fuse_entry_param* e, const string& child_path,
                      int& backing_fd) {
    // TODO(b/211873756) Enable only for the primary volume. Must be
    // extended for other media devices.
    if (android::base::StartsWith(child_path, PRIMARY_VOLUME_PREFIX)) {
        if (is_bpf_backing_path(child_path)) {
            fuse_bpf_fill_entries(child_path, fuse->bpf_fd.get(), e, backing_fd);
        } else if (is_package_owned_path(child_path, fuse->path)) {
            fuse_bpf_fill_entries(child_path, static_cast<int>(BpfFd::REMOVE), e, backing_fd);
        }
    }
}
```

目前仅仅为 is_bpf_backing_path = true ，也就是 Android/data, Android/obb 设置 backing_fd

```cpp
const std::regex PATTERN_BPF_BACKING_PATH("^/storage/[^/]+/[0-9]+/Android/(data|obb)$",
                                          std::regex_constants::icase);

static bool is_bpf_backing_path(const string& path) {
    return std::regex_match(path, PATTERN_BPF_BACKING_PATH);
}
```

这个 backing_fd 自然是下层的 fd ，也就是 `/data/media/$USER/Android/{data,obb}`

```cpp
void fuse_bpf_fill_entries(const string& path, const int bpf_fd, struct fuse_entry_param* e,
                           int& backing_fd) {
    /*
     * The file descriptor `backing_fd` must not be closed as it is closed
     * automatically by the kernel as soon as it consumes the FUSE reply. This
     * mechanism is necessary because userspace doesn't know when the kernel
     * will consume the FUSE response containing `backing_fd`, thus it may close
     * the `backing_fd` too soon, with the risk of assigning a backing file
     * which is either invalid or corresponds to the wrong file in the lower
     * file system.
     */
    backing_fd = open(path.c_str(), O_CLOEXEC | O_DIRECTORY | O_RDONLY);
    if (backing_fd < 0) {
        PLOG(ERROR) << "Failed to open: " << path;
        return;
    }

    e->backing_action = FUSE_ACTION_REPLACE;
    e->backing_fd = backing_fd;

    if (bpf_fd >= 0) {
        e->bpf_action = FUSE_ACTION_REPLACE;
        e->bpf_fd = bpf_fd;
    } else if (bpf_fd == static_cast<int>(BpfFd::REMOVE)) {
        e->bpf_action = FUSE_ACTION_REMOVE;
    } else {
        e->bpf_action = FUSE_ACTION_KEEP;
    }
}
```

这里需要解释一下 fuse bpf 给 fuse_entry_param 增加的几个扩展的返回字段：

```cpp
        uint64_t        backing_action;
        uint64_t        backing_fd;
        uint64_t        bpf_action;
        uint64_t        bpf_fd;
```

这些实际上是 fuse_entry_bpf_out 的字段：

```cpp
struct fuse_entry_bpf_out {
        uint64_t        backing_action;
        uint64_t        backing_fd;
        uint64_t        bpf_action;
        uint64_t        bpf_fd;
};
```

其中 backing_action 和 backing_fd 是一对，action 包括 FUSE_ACTION_REPLACE, FUSE_ACTION_REMOVE, FUSE_ACTION_KEEP ，表示对 backing fd 的操作：保持，替换或移除。backing fd 就是下层文件系统的文件对象的 fd 。

```cpp
#define FUSE_ACTION_KEEP        0
#define FUSE_ACTION_REMOVE      1
#define FUSE_ACTION_REPLACE     2
```

bpf_action 和 bpf_fd 同理也是一对，表示对 bpf prog 的操作：保持，替换或移除，这里 bpf 是打开 /sys/fs/bpf/... 获得的 bpf program 。

https://cs.android.com/android/platform/superproject/main/+/main:system/bpfprogs/fuseMedia.c;l=2?q=fuseMedia

Media Provider 的 bpf 程序 /sys/fs/bpf/prog_fuseMedia_fuse_media 代码如下：

```c
DEFINE_BPF_PROG("fuse/media", AID_ROOT, AID_MEDIA_RW, fuse_media)
(struct fuse_bpf_args* fa) {
    switch (fa->opcode) {
        case FUSE_LOOKUP | FUSE_PREFILTER: {
            const char* name = fa->in_args[0].value;

            bpf_printk("LOOKUP_PREFILTER: %lx %s", fa->nodeid, name);
            return FUSE_BPF_BACKING | FUSE_BPF_POST_FILTER;
        }

        case FUSE_LOOKUP | FUSE_POSTFILTER: {
            struct fuse_entry_out* feo = fa->out_args[0].value;
            struct fuse_entry_bpf_out* febo = fa->out_args[1].value;
            uint64_t uid_gid = bpf_get_current_uid_gid();
            uint32_t uid = uid_gid;
            uint32_t gid = uid_gid >> 32;

            febo->bpf_action = FUSE_ACTION_REMOVE;

            /* If the decision is easy, make it here for performance */
            if (fa->error_in || (feo->attr.mode & 0001) ||
                ((feo->attr.mode & 0010) && gid == feo->attr.gid) ||
                ((feo->attr.mode & 0100) && uid == feo->attr.uid))
                return 0;

            /* Delegate to the daemon */
            return FUSE_BPF_USER_FILTER;
        }

        case FUSE_READDIR | FUSE_PREFILTER: {
            return FUSE_BPF_BACKING | FUSE_BPF_POST_FILTER;
        }

        case FUSE_READDIR | FUSE_POSTFILTER: {
            return FUSE_BPF_USER_FILTER;
        }

        default:
            return FUSE_BPF_BACKING;
    }
}
```

Fuse media bpf 中，对于 Android/data 目录的 lookup ，只执行简单的权限检查，即如果下层文件系统的 uid/gid 匹配，就直接透传给下层，否则让 Fuse Daemon 决定(`return FUSE_BPF_USER_FILTER`)，此外 lookup 完成后要移除新的 inode 的 bpf prog （默认是继承的），因为 Android/data/pkg 目录不需要使用这个 bpf prog 进行过滤。如果 postfilter 请求传入了 Fuse Daemon ，它会判断目标路径是否在 caller uid 的可访问范围内，如果不是则返回 ENOENT ，这同时解决了包名存在性探测的问题。

然而 Fuse Daemon 对于 fuse bpf 的配置也没有考虑到可忽略码点， [`is_bpf_backing_path`](https://cs.android.com/android/platform/superproject/main/+/main:packages/providers/MediaProvider/jni/FuseDaemon.cpp;l=488-490;drc=61197364367c9e404c7da6900658f1b16c42d0da) 使用正则表达式匹配 Android/data 或 obb ，显然无法匹配带有可忽略码点的路径。因此 FuseFixer 也需要 hook 这个函数来修复这个问题。

## 修复 Fuse BPF 后仍然存在的问题

尽管 FuseFixer 在某种程度上修复了 Fuse Daemon 配置 Fuse BPF 的问题，但仍然存在两个致命问题，导致检测仍可能生效：

### 1. 使用中的 dentry 无法被拦截

如果某个 dentry 处于使用中，即被某个进程打开，或者如果它是一个目录，而其子目录被使用，那么 lookup 的时候总会使用 dcache 中缓存的这个 dentry ，不经过 fuse bpf 的 lookup ，也就无法让 fuse daemon 返回 ENOENT 来拦截访问。

为此需要修改内核来解决这个问题，即在 fuse dentry revalidate 实现对 backing inode 重新发起请求，我向 Android kernel 提供了补丁:  [3943780](https://android-review.googlesource.com/c/kernel/common/+/3943780) ，不过仍未合并，有需要的可以尝试合入这个补丁。

### 2. mkdir/rename 等系统调用的返回值问题

如果使用 mkdir 创建一个原本确实不存在的目录，只要路径上都有 x 权限（允许 lookup），而目标目录无 w 权限，则应该返回 EACCES ；如果目录确实存在，应该返回 EEXIST 。然而 fuse bpf 情况下，如果给被隐藏的包名目录执行 mkdir ，却会返回 ENOENT ，这是因为在 lookup 过程中，fuse bpf 返回的是错误而不是 NULL 。rename 也有类似问题。相关源码： [lookup_one_qstr_excl](https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/fs/namei.c;l=1785-1823;drc=7ec1b54315d02cd8edd3469784e9ca4438cb863e;bpv=1;bpt=1)

这个问题甚至难以通过修改内核解决，因为本质上关系到了 fuse bpf 的设计问题；因此也更无法通过 FuseFixer 修复，或许需要采用其他思路修复。
