/***************************************************************************
 *   Copyright © 2025 by Andrey Afletdinov <public.irkutsk@gmail.com>      *
 *                                                                         *
 *   Part of the InotifyWatcher                                            *
 *   https://github.com/AndreyBarmaley/inotify-watcher                     *
 *                                                                         *
 *   MIT License                                                           *
 *                                                                         *
 ***************************************************************************/

#ifndef _INOTIFY_TOOLS_
#define _INOTIFY_TOOLS_

#include <list>
#include <string>
#include <filesystem>
#include <forward_list>

enum class ReadDirFilter { All, Dir, File };

namespace Inotify {
    const char* maskToName(uint32_t mask);
    uint32_t nameToMask(std::string_view name);
}

namespace System {
    std::forward_list<std::string> readDir(const std::filesystem::path & path, bool recursive, const ReadDirFilter & filter = ReadDirFilter::All);
    void runCommand(std::string cmd, std::list<std::string> args, std::string owner);
}

#endif
