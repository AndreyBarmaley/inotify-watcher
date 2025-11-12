/***************************************************************************
 *   Copyright © 2025 by Andrey Afletdinov <public.irkutsk@gmail.com>      *
 *                                                                         *
 *   Part of the InotifyWatcher                                            *
 *   https://github.com/AndreyBarmaley/inotify-watcher                     *
 *                                                                         *
 *   MIT License                                                           *
 *                                                                         *
 ***************************************************************************/

#include <stdexcept>
#include <functional>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

#include"inotify_path.h"

using namespace boost;

namespace Inotify {
    void Path::cancelAsync(void) {
        sd_.cancel();
    }

    bool Path::parseEvents(const char* beg, const char* end) {
        while(beg < end) {
            auto st = (struct inotify_event*) beg;

            if(beg + sizeof(struct inotify_event) + st->len > end) {
                log->error("{}: read invalid, name overbuf, len: {}", __FUNCTION__,  st->len);
                return false;
            }

            if(st->mask & (IN_IGNORED)) {
                beg += sizeof(struct inotify_event) + st->len;
                continue;
            }

            std::string name;

            if(st->len) {
                name.assign(st->name);
            }

            if(st->mask & (IN_CREATE)) {
                asio::post(ioc_, std::bind(& Path::inCreateEvent, this, path_, std::move(name)));
            }

            if(st->mask & (IN_OPEN)) {
                asio::post(ioc_, std::bind(& Path::inOpenEvent, this, path_, std::move(name)));
            }

            if(st->mask & (IN_ACCESS)) {
                asio::post(ioc_, std::bind(& Path::inAccessEvent, this, path_, std::move(name)));
            }

            if(st->mask & (IN_MODIFY)) {
                asio::post(ioc_, std::bind(& Path::inModifyEvent, this, path_, std::move(name)));
            }

            if(st->mask & (IN_ATTRIB)) {
                asio::post(ioc_, std::bind(& Path::inAttribEvent, this, path_, std::move(name)));
            }

            if(st->mask & (IN_CLOSE_WRITE)) {
                asio::post(ioc_, std::bind(& Path::inCloseEvent, this, path_, std::move(name), true));
            }

            if(st->mask & (IN_CLOSE_NOWRITE)) {
                asio::post(ioc_, std::bind(& Path::inCloseEvent, this, path_, std::move(name), false));
            }

            if(st->mask & (IN_MOVE)) {
                asio::post(ioc_, std::bind(& Path::inMoveEvent, this, path_, std::move(name), false));
            }

            if(st->mask & (IN_MOVE_SELF)) {
                asio::post(ioc_, std::bind(& Path::inMoveEvent, this, path_, std::move(name), true));
            }

            if(st->mask & (IN_DELETE)) {
                asio::post(ioc_, std::bind(& Path::inDeleteEvent, this, path_, std::move(name), false));
            }

            if(st->mask & (IN_DELETE_SELF)) {
                asio::post(ioc_, std::bind(& Path::inDeleteEvent, this, path_, std::move(name), true));
            }

            beg += sizeof(struct inotify_event) + st->len;
        }

        return true;
    }

    void Path::readNotify(const system::error_code & ec, size_t recv) {
        if(ec) {
            // ref: https://stackoverflow.com/questions/21046742/using-boostsystemerror-code-in-c
            if(ec.value() != system::errc::operation_canceled) {
                log->error("{}: {} error, code: {}, message: {}", __FUNCTION__, "read", ec.value(), ec.message());
            }

            return;
        }

        if(! parseEvents(buf_.data(), buf_.data() + recv)) {
            return;
        }

        // next async
        asio::async_read(sd_, asio::buffer(buf_), asio::transfer_at_least(sizeof(struct inotify_event)),
                         asio::bind_executor(strand_, std::bind(& Path::readNotify, this, std::placeholders::_1, std::placeholders::_2)));
    };

    Path::Path(asio::io_context & ioc, const std::filesystem::path & path, uint32_t events)
<<<<<<< HEAD
        : sd_(ioc), strand_(ioc.get_executor()), path_(path), ioc_(ioc) {
=======
        : ioc_(ioc), sd_(ioc), strand_(ioc.get_executor()), path_(path) {
>>>>>>> 2b0bc0e (Without Boost::json version)

        log = spdlog::get("inotify_watcher");

        if(! std::filesystem::exists(path_)) {
            log->error("path not exists: {}", path_.c_str());
            throw std::runtime_error(__FUNCTION__);
        }

        fd_ = inotify_init1(IN_NONBLOCK);

        if(fd_ < 0) {
            log->error("{}: {} failed, error: {}, errno: {}", __FUNCTION__, "inotify_init", strerror(errno), errno);
            throw std::runtime_error(__FUNCTION__);
        }

        // watch directory for any activity and report it back to me
        wd_ = inotify_add_watch(fd_, path_.c_str(), events);

        if(wd_ < 0) {
            log->error("{}: {} failed, error: {}, errno: {}", __FUNCTION__, "inotify_add_watch", strerror(errno), errno);
            throw std::runtime_error(__FUNCTION__);
        }

        sd_.assign(fd_);
        log->info("target: {}", path.native());

        asio::async_read(sd_, asio::buffer(buf_), asio::transfer_at_least(sizeof(struct inotify_event)),
                         asio::bind_executor(strand_, std::bind(& Path::readNotify, this, std::placeholders::_1, std::placeholders::_2)));
    }

    Path::~Path() {
        sd_.cancel();

        if(0 <= wd_) {
            inotify_rm_watch(fd_, wd_);
        }

        if(0 <= fd_) {
            close(fd_);
        }
    }
}
