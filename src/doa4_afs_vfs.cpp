// doa4_afs_vfs.cpp
// The combined archives in the VFS. See doa4_afs_vfs.h.

#include "doa4_afs_vfs.h"

#include <algorithm>
#include <mutex>
#include <set>
#include <span>
#include <vector>

#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/logging.h>
#include <rex/system/xtypes.h>

namespace doa4::afs {

namespace {

using rex::X_STATUS;  // the X_STATUS_* codes are macros that cast to the unqualified name
using rex::filesystem::Entry;
using rex::filesystem::File;

class OverlayEntry final : public Entry {
 public:
  OverlayEntry(Entry* original, std::shared_ptr<const Overlay> overlay, std::string archive,
               std::map<uint32_t, std::string> labels)
      : Entry(original->device(), original->parent(), original->path()),
        overlay_(std::move(overlay)),
        archive_(std::move(archive)),
        labels_(std::move(labels)) {
    attributes_ = original->attributes();
    size_ = size_t(overlay_->size());
    const size_t sector = std::max<size_t>(1, device_->bytes_per_sector());
    allocation_size_ = (size_ + sector - 1) / sector * sector;
    create_timestamp_ = original->create_timestamp();
    access_timestamp_ = original->access_timestamp();
    write_timestamp_ = original->write_timestamp();
    via_ = device_->mount_path();
  }

  X_STATUS Open(uint32_t desired_access, File** out_file) override;

  // The replaced original lives as long as this entry (nothing may be left pointing at freed
  // memory; the VFS owns this entry and destroys it with the device).
  void Keep(std::unique_ptr<Entry> original) { original_ = std::move(original); }
  const Overlay& overlay() const { return *overlay_; }

  // Logs the first read of each replaced entry: evidence the game loaded the replaced bytes. There
  // is one OverlayEntry per device and archive, so this is once per (device, archive, entry), and
  // the line ends with the device the VFS opened (" via \Device\Cdrom0"), so a read through each
  // device tree shows.
  void NoteRead(uint64_t offset) {
    const int entry = overlay_->ReplacedEntryAt(offset);
    if (entry < 0) return;
    std::lock_guard<std::mutex> lock(seen_mutex_);
    if (!seen_.insert(entry).second) return;
    const auto it = labels_.find(uint32_t(entry));
    REXLOG_INFO("[doa4][AFS] {} entry {} read: {} via {}", archive_, entry,
                it == labels_.end() ? std::string("?") : it->second, via_);
  }

 private:
  std::shared_ptr<const Overlay> overlay_;
  std::string archive_;
  std::string via_;  // the device's mount path, e.g. \Device\Cdrom0
  std::map<uint32_t, std::string> labels_;
  std::unique_ptr<Entry> original_;
  std::mutex seen_mutex_;
  std::set<int> seen_;
};

class OverlayFile final : public File {
 public:
  OverlayFile(uint32_t access, OverlayEntry* entry) : File(access, entry), overlay_entry_(entry) {}
  void Destroy() override { delete this; }

  X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset, size_t* out_bytes_read) override {
    const Overlay& o = overlay_entry_->overlay();
    if (byte_offset >= o.size()) return X_STATUS_END_OF_FILE;
    const size_t n = size_t(std::min<uint64_t>(buffer.size(), o.size() - byte_offset));
    if (!o.Read(byte_offset, buffer.data(), n)) {
      REXLOG_ERROR("[doa4][AFS] read failed at {:#x} (+{:#x})", byte_offset, n);
      return X_STATUS_UNSUCCESSFUL;
    }
    overlay_entry_->NoteRead(byte_offset);
    *out_bytes_read = n;
    return X_STATUS_SUCCESS;
  }

  X_STATUS WriteSync(std::span<const uint8_t>, size_t, size_t*) override { return X_STATUS_ACCESS_DENIED; }

 private:
  OverlayEntry* overlay_entry_;
};

X_STATUS OverlayEntry::Open(uint32_t desired_access, File** out_file) {
  *out_file = new OverlayFile(desired_access, this);
  return X_STATUS_SUCCESS;
}

}  // namespace

std::string InstallOverlays(rex::filesystem::VirtualFileSystem& vfs,
                            const std::map<std::string, std::shared_ptr<const Overlay>>& overlays,
                            const std::map<std::string, std::map<uint32_t, std::string>>& labels) {
  static constexpr const char* kDevices[] = {"\\Device\\Cdrom0", "\\Device\\Harddisk0\\Partition1"};
  struct Target {
    Entry* original;
    std::string archive;
    std::shared_ptr<const Overlay> overlay;
  };
  std::vector<Target> targets;
  for (const char* device : kDevices) {
    for (const auto& [name, overlay] : overlays) {
      const std::string path = std::string(device) + "\\" + name;
      Entry* e = vfs.ResolvePath(path);
      if (!e || !e->parent() || (e->attributes() & rex::filesystem::kFileAttributeDirectory)) {
        return "cannot find " + path;
      }
      if (e->size() != overlay->original_size()) {
        return path + " is " + std::to_string(e->size()) + " bytes, the game folder's " + name +
               " is " + std::to_string(overlay->original_size());
      }
      targets.push_back({e, name, overlay});
    }
  }
  for (const Target& t : targets) {
    Entry* parent = t.original->parent();
    const auto it = labels.find(t.archive);
    auto entry = std::make_unique<OverlayEntry>(
        t.original, t.overlay, t.archive,
        it == labels.end() ? std::map<uint32_t, std::string>() : it->second);
    OverlayEntry* raw = entry.get();
    raw->Keep(parent->ReplaceChild(std::move(entry)));
    REXLOG_INFO("[doa4][AFS] {} serves the combined {} ({:#x} bytes, original {:#x})",
                t.original->absolute_path(), t.archive, t.overlay->size(), t.overlay->original_size());
  }
  return {};
}

}  // namespace doa4::afs
