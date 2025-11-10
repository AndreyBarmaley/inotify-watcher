/***************************************************************************
 *   Copyright © 2025 by Andrey Afletdinov <public.irkutsk@gmail.com>      *
 *                                                                         *
 *   Part of the InotifyWatcher                                            *
 *   https://github.com/AndreyBarmaley/inotify-watcher                     *
 *                                                                         *
 *   MIT License                                                           *
 *                                                                         *
 ***************************************************************************/

#ifndef INOTIFY_PATH_H_
#define INOTIFY_PATH_H_

#include <boost/asio.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/asio/io_context_strand.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>

#include <sys/inotify.h>

#include <array>
#include <stdexcept>
#include <filesystem>

namespace Inotify {
    class Path : boost::noncopyable {
        int fd_ = -1;
        int wd_ = -1;

        boost::asio::posix::stream_descriptor sd_;
        boost::asio::strand<boost::asio::io_context::executor_type> strand_;

        std::filesystem::path path_;
        std::array<char, 1024> buf_;

      protected:
        boost::asio::io_context & ioc_;

        void cancelAsync(void);
        bool parseEvents(const char* beg, const char* end);
        void readNotify(const boost::system::error_code & ec, size_t recv);

      public:
        Path(boost::asio::io_context &, const std::filesystem::path &, uint32_t events = IN_ALL_EVENTS);
        virtual ~Path();

        virtual void inOpenEvent(const std::filesystem::path &, std::string) {}
        virtual void inAccessEvent(const std::filesystem::path &, std::string) {}
        virtual void inModifyEvent(const std::filesystem::path &, std::string) {}
        virtual void inAttribEvent(const std::filesystem::path &, std::string) {}
        virtual void inCloseEvent(const std::filesystem::path &, std::string, bool write) {}
        virtual void inMoveEvent(const std::filesystem::path &, std::string, bool self) {}
        virtual void inCreateEvent(const std::filesystem::path &, std::string) {}
        virtual void inDeleteEvent(const std::filesystem::path &, std::string, bool self) {}
        
        inline bool isPath(const std::filesystem::path & path) const { return path == path_; }
    };
}

#endif // INOTIFY_PATH_H_
