// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <ddk/device.h>
#include <fs/vfs.h>
#include <magenta/compiler.h>
#include <magenta/thread_annotations.h>
#include <magenta/types.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#ifdef __cplusplus

#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

#include "dnode.h"

namespace memfs {

class Dnode;

struct VnodeWatcher : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<VnodeWatcher>> {
public:
    VnodeWatcher();
    ~VnodeWatcher();

    mx_handle_t h;
};

class VnodeMemfs : public fs::Vnode {
public:
    virtual mx_status_t GetHandles(uint32_t flags, mx_handle_t* hnds,
                                   uint32_t* type, void* extra, uint32_t* esize) override;
    virtual void Release() override;
    virtual mx_status_t Open(uint32_t flags) override;
    virtual mx_status_t Close() override;
    virtual mx_status_t Setattr(vnattr_t* a) override;
    virtual mx_status_t Sync() override;
    ssize_t Ioctl(uint32_t op, const void* in_buf,
                  size_t in_len, void* out_buf, size_t out_len) final;
    mx_status_t AttachRemote(mx_handle_t h) final;

    // To be more specific: Is this vnode connected into the directory hierarchy?
    // VnodeDirs can be unlinked, and this method will subsequently return false.
    bool IsDirectory() const { return dnode_ != nullptr; }

    virtual ~VnodeMemfs();

    // TODO(smklein): The following members should become private
    uint32_t seqcount_;

    Dnode::DeviceList devices_; // All devices pointing to this vnode
    mxtl::RefPtr<Dnode> dnode_;
    uint32_t link_count_;

protected:
    VnodeMemfs();

    uint64_t create_time_;
    uint64_t modify_time_;
};

class VnodeFile final : public VnodeMemfs {
public:
    VnodeFile();
    ~VnodeFile();

private:
    void Release() final;
    ssize_t Read(void* data, size_t len, size_t off) final;
    ssize_t Write(const void* data, size_t len, size_t off) final;
    mx_status_t Truncate(size_t len) final;
    mx_status_t Getattr(vnattr_t* a) final;

    mx_handle_t vmo_;
    mx_off_t length_;
};

class VnodeDir : public VnodeMemfs {
public:
    VnodeDir();
    virtual ~VnodeDir();

    mx_status_t Lookup(fs::Vnode** out, const char* name, size_t len) final;
    mx_status_t Create(fs::Vnode** out, const char* name, size_t len, uint32_t mode) final;

    // Create a vnode from a VMO.
    // Fails if the vnode already exists.
    // Passes the vmo to the Vnode; does not duplicate it.
    mx_status_t CreateFromVmo(const char* name, size_t namelen, mx_handle_t vmo, mx_off_t off,
                              mx_off_t len);

    mx_status_t CreateDeviceAtLocked(memfs::VnodeDir** out, const char* name, mx_handle_t h);
    void NotifyAdd(const char* name, size_t len) TA_REQ(vfs_lock) final;
private:
    mx_status_t IoctlWatchDir(const void* in_buf, size_t in_len, void* out_buf,
                              size_t out_len) final;
    mx_status_t Readdir(void* cookie, void* dirents, size_t len) final;

    // Resolves the question, "Can this directory create a child node with the name?"
    // Returns "NO_ERROR" on success; otherwise explains failure with error message.
    mx_status_t CanCreate(const char* name, size_t namelen) const;

    // Creates a dnode for the Vnode, attaches vnode to dnode, (if directory) attaches
    // dnode to vnode, and adds dnode to parent directory.
    mx_status_t AttachVnode(memfs::VnodeMemfs* vn, const char* name, size_t namelen, bool isdir);

    mx_status_t Unlink(const char* name, size_t len, bool must_be_dir) final;
    mx_status_t Rename(fs::Vnode* newdir,
                       const char* oldname, size_t oldlen,
                       const char* newname, size_t newlen,
                       bool src_must_be_dir, bool dst_must_be_dir) final;
    mx_status_t Link(const char* name, size_t len, fs::Vnode* target) final;
    mx_status_t Getattr(vnattr_t* a) override;

    // TODO(smklein): Guard the watch list with a lock more fine-grained
    // than the VFS lock (or make the watch list thread-safe while lock-free).
    mxtl::DoublyLinkedList<mxtl::unique_ptr<VnodeWatcher>> watch_list_ TA_GUARDED(vfs_lock);
};

class VnodeDevice final : public VnodeDir {
public:
    VnodeDevice();
    ~VnodeDevice();

private:
    void Release() final;
    mx_status_t Getattr(vnattr_t* a) final;
    mx_status_t GetHandles(uint32_t flags, mx_handle_t* hnds,
                           uint32_t* type, void* extra, uint32_t* esize) final;
};

class VnodeVmo final : public VnodeMemfs {
public:
    VnodeVmo(mx_handle_t vmo, mx_off_t offset, mx_off_t length);
    ~VnodeVmo();

private:
    ssize_t Read(void* data, size_t len, size_t off) final;
    ssize_t Write(const void* data, size_t len, size_t off) final;
    mx_status_t Getattr(vnattr_t* a) final;
    mx_status_t GetHandles(uint32_t flags, mx_handle_t* hnds,
                           uint32_t* type, void* extra, uint32_t* esize) final;

    mx_handle_t vmo_;
    mx_off_t offset_;
    mx_off_t length_;
};

} // namespace memfs

using VnodeMemfs = memfs::VnodeMemfs;
using VnodeDir = memfs::VnodeDir;

#else

typedef struct VnodeMemfs VnodeMemfs;
typedef struct VnodeDir VnodeDir;

#endif  // ifdef __cplusplus

__BEGIN_CDECLS

void vfs_global_init(VnodeDir* root);

// generate mxremoteio handles
mx_handle_t vfs_create_global_root_handle(void);
mx_handle_t vfs_create_root_handle(VnodeMemfs* vn);

// device fs
VnodeDir* devfs_get_root(void);
mx_status_t memfs_create_device_at(VnodeDir* parent, VnodeDir** out, const char* name,
                                   mx_handle_t hdevice) TA_EXCL(vfs_lock);
mx_status_t devfs_remove(VnodeDir* vn);

// boot fs
VnodeDir* bootfs_get_root(void);
mx_status_t bootfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, size_t len);

// system fs
VnodeDir* systemfs_get_root(void);
mx_status_t systemfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, size_t len);

// memory fs
VnodeDir* memfs_get_root(void);
mx_status_t memfs_add_link(VnodeDir* parent, const char* name,
                           VnodeMemfs* target) TA_EXCL(vfs_lock);

// Create the global root to memfs
VnodeDir* vfs_create_global_root(void) TA_NO_THREAD_SAFETY_ANALYSIS;

// Create a generic root to memfs
VnodeDir* vfs_create_root(void);

// shared among all memory filesystems
mx_status_t memfs_create_directory(const char* path, uint32_t flags);
void memfs_mount(VnodeDir* parent, VnodeDir* subtree) TA_EXCL(vfs_lock);

__END_CDECLS
