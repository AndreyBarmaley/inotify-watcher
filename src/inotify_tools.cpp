/***************************************************************************
 *   Copyright © 2025 by Andrey Afletdinov <public.irkutsk@gmail.com>      *
 *                                                                         *
 *   Part of the InotifyWatcher                                            *
 *   https://github.com/AndreyBarmaley/inotify-watcher                     *
 *                                                                         *
 *   MIT License                                                           *
 *                                                                         *
 ***************************************************************************/

#include <pwd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <spdlog/spdlog.h>

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/inotify.h>

#include <iostream>
#include "inotify_tools.h"

namespace Inotify {
    auto allMasks = { IN_OPEN, IN_MODIFY, IN_ATTRIB, IN_ACCESS, IN_CLOSE_WRITE,
             IN_CREATE, IN_CLOSE_NOWRITE, IN_DELETE_SELF, IN_DELETE, IN_MOVE_SELF, IN_MOVED_FROM, IN_MOVED_TO
    };

    const char* maskToName(uint32_t mask) {
        switch(mask) {
            case IN_ALL_EVENTS:
                return "IN_ALL_EVENTS";

            case IN_OPEN:
                return "IN_OPEN";

            case IN_MODIFY:
                return "IN_MODIFY";

            case IN_ATTRIB:
                return "IN_ATTRIB";

            case IN_ACCESS:
                return "IN_ACCESS";

            case IN_CLOSE_WRITE:
                return "IN_CLOSE_WRITE";

            case IN_CREATE:
                return "IN_CREATE";

            case IN_CLOSE_NOWRITE:
                return "IN_CLOSE_NOWRITE";

            case IN_DELETE_SELF:
                return "IN_DELETE_SELF";

            case IN_DELETE:
                return "IN_DELETE";

            case IN_MOVE:
                return "IN_MOVE";

            case IN_MOVE_SELF:
                return "IN_MOVE_SELF";

            case IN_MOVED_FROM:
                return "IN_MOVED_FROM";

            case IN_MOVED_TO:
                return "IN_MOVED_TO";

            default:
                break;
        }

        return nullptr;
    }

    uint32_t nameToMask(std::string_view name1) {
        for(const auto & mask : allMasks) {
            if(auto name2 = maskToName(mask); 0 == name1.compare(name2)) {
                return mask;
            }
        }

        return 0;
    }
}

namespace System {
    std::forward_list<std::string> readDirSub(const std::filesystem::path & path, bool recursive, const ReadDirFilter & filter) {
        std::forward_list<std::string> res;

        for(auto const & entry : std::filesystem::directory_iterator{path}) {
            bool insert = false;

            switch(filter) {
                case ReadDirFilter::File:
                    insert = entry.is_regular_file();
                    break;

                case ReadDirFilter::Dir:
                    insert = entry.is_directory();
                    break;

                case ReadDirFilter::All:
                    insert = true;
                    break;
            }

            if(insert) {
                res.push_front(std::filesystem::absolute(entry.path()).native());
            }

            if(entry.is_directory() && recursive) {
                auto subdir = readDirSub(std::filesystem::absolute(entry.path()), recursive, filter);

                if(! subdir.empty())
                    res.splice_after(res.begin(), subdir);
            }
        }

        return res;
    }

    std::forward_list<std::string> readDir(const std::filesystem::path & path, bool recursive, const ReadDirFilter & filter) {

        if(! std::filesystem::is_directory(path)) {
            return {};
        }

        auto res = readDirSub(path, recursive, filter);

        if(filter != ReadDirFilter::File) {
            res.push_front(path.native());
        }

        return res;
    }

    void runCommand(std::string cmd, std::list<std::string> args, std::string owner) {
        pid_t pid = fork();

        if(0 < pid) {
            int status;

            if(0 > waitpid(pid, & status, 0)) {
                spdlog::error("{}: {} failed, error: {}, errno: {}", __FUNCTION__, "waitpid", strerror(errno), errno);
            }

            return;

        } else if(0 > pid) {
            spdlog::error("{}: {} failed, error: {}, errno: {}", __FUNCTION__, "fork", strerror(errno), errno);
            return;
        }

        // child mode: pid == 0
        for(int fd = 0; fd < 256; ++fd) {
            close(fd);
        }

        // stdin
        open("/dev/null", O_RDONLY);
        // stdout
        open("/dev/null", O_WRONLY | O_APPEND);
        // stderr
        open("/dev/null", O_WRONLY | O_APPEND);

        // goto tmp
        chdir("/tmp");

        if(0 == getuid() && ! owner.empty() && owner != "root") {
            struct passwd* pwd = getpwnam(owner.c_str());

            if(pwd && pwd->pw_uid) {
                setgid(pwd->pw_gid);

                if(0 > setuid(pwd->pw_uid)) {
                    spdlog::error("{}: {} failed, error: {}, errno: {}", __FUNCTION__, "setuid", strerror(errno), errno);
                    std::exit(-1);
                }

                setenv("USER", pwd->pw_name, 1);
                setenv("LOGNAME", pwd->pw_name, 1);
                setenv("HOME", pwd->pw_dir, 1);
            }
        }

        std::vector<const char*> argv;
        argv.reserve(args.size() + 2);

        argv.push_back(cmd.c_str());

        for(auto & val : args) {
            argv.push_back(val.c_str());
        }

        argv.push_back(nullptr);

        execv(cmd.c_str(), (char* const*) argv.data());
        std::exit(-1);
    }
}
