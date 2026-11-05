// doa4_afs_vfs.h
// A combined archive in the game's file system: an entry that serves an
// Overlay takes the original archive's place in the folder trees behind d: (\Device\Cdrom0)
// and cache: (\Device\Harddisk0\Partition1), which both serve the game folder (spec 3.2).

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <rex/filesystem/vfs.h>

#include "doa4_afs_overlay.h"

namespace doa4::afs {

// Installs every overlay on both devices, or nothing: all entries are found and checked (the
// original's size equals the overlay's original size) before any is replaced. Returns "" or why
// nothing was installed. `labels`: per archive, the replaced entries' names for the log.
std::string InstallOverlays(rex::filesystem::VirtualFileSystem& vfs,
                            const std::map<std::string, std::shared_ptr<const Overlay>>& overlays,
                            const std::map<std::string, std::map<uint32_t, std::string>>& labels);

}  // namespace doa4::afs
