# Inotify Watcher Service

**Inotify Watcher Service** is a lightweight daemon written in **C++** using the **Boost.Asio** library, designed to monitor filesystem changes through the **Linux inotify** mechanism. When file changes are detected (creation, modification, deletion), the service executes user-defined commands specified in a configuration file.

## Key Features

- File and directory change monitoring via **inotify**
- Asynchronous event processing using **Boost.Asio**
- Flexible configuration through config file (format: JSON)
- Ability to execute arbitrary shell commands on trigger events
- Support for monitoring multiple paths simultaneously
- Lightweight and efficient, suitable for production environments

## Technologies

- **Programming Language**: C++
- **Asynchronous I/O**: Boost.Asio
- **Filesystem Monitoring**: Linux inotify API
- **Configuration Format**: JSON

## Configuration Example

```json
{
  "debug": false,
  "jobs": [
    {
      "path": "/var/www/html",
      "inotify:details": "see man inotify",
      "inotify": [ "IN_CLOSE_WRITE", "IN_CREATE", "IN_DELETE" ],
      "recursive": false,
      "owner:details": "the command will run under this user if the service has admin rights",
      "owner": "user1",
      "command": "/usr/bin/logger"
    },
    {
      "path": "/etc/myapp/config.ini",
      "owner": "user2",
      "command": "/usr/bin/logger"
    }
  ]
}
```

## Installation and Running

### Building from source:

```bash
mkdir build && cd build
cmake ..
make
```

### Running the service:

```bash
./inotify_watcher --config /path/to/config.json
```

The service automatically updates the status when the configuration file is changed.

#### Default config path:
- from system `/etc/inotify_watcher/config.json`
- from environment `INOTIFY_SERVICE_CONF`
- from cmd arg `--config /path_to/config.json`

### Monitoring the service:
- set `debug` to `true` on config
- set `command` to `/usr/bin/logger` for job
- run command `journalctl -t inotify_watcher -f`

```log
inotify_watcher[1093675]: target: /etc/inotify_watcher
inotify_watcher[1093675]: found config: /etc/inotify_watcher/config.json
inotify_watcher[1093675]: readConfig: success
inotify_watcher[1093675]: parseJobs: job skipped, path not found: /var/tmp/123
inotify_watcher[1093675]: target: /var/tmp
inotify_watcher[1093675]: AddNotify: add job, path: /var/tmp, name: dracut.log
inotify_watcher[1093675]: inOpenEvent: path: /var/tmp, name:
inotify_watcher[1093675]: inOpenEvent: run cmd: /usr/bin/logger, args: [IN_OPEN,/var/tmp]
inotify_watcher[1093675]: inAccessEvent: path: /var/tmp, name:
inotify_watcher[1093675]: inAccessEvent: run cmd: /usr/bin/logger, args: [IN_ACCESS,/var/tmp]
inotify_watcher[1093675]: inAccessEvent: path: /var/tmp, name:
inotify_watcher[1093675]: inAccessEvent: run cmd: /usr/bin/logger, args: [IN_ACCESS,/var/tmp]
inotify_watcher[1093675]: inAccessEvent: path: /var/tmp, name:
inotify_watcher[1093675]: inAccessEvent: run cmd: /usr/bin/logger, args: [IN_ACCESS,/var/tmp]
inotify_watcher[1093675]: inCloseEvent: path: /var/tmp, name: , write: false
inotify_watcher[1093675]: inOpenEvent: path: /var/tmp, name: dracut.log
inotify_watcher[1093675]: inOpenEvent: run cmd: /usr/bin/logger, args: [IN_OPEN,/var/tmp/dracut.log]
inotify_watcher[1093675]: inAccessEvent: path: /var/tmp, name: dracut.log
inotify_watcher[1093675]: inAccessEvent: run cmd: /usr/bin/logger, args: [IN_ACCESS,/var/tmp/dracut.log]
inotify_watcher[1093675]: inAccessEvent: path: /var/tmp, name: dracut.log
inotify_watcher[1093675]: inAccessEvent: run cmd: /usr/bin/logger, args: [IN_ACCESS,/var/tmp/dracut.log]
inotify_watcher[1093675]: inCloseEvent: path: /var/tmp, name: dracut.log, write: false
inotify_watcher[1093675]: inCloseEvent: run cmd: /usr/bin/logger, args: [IN_CLOSE_NOWRITE,/var/tmp/dracut.log]
inotify_watcher[1093675]: inOpenEvent: path: /var/tmp, name: dracut.log
inotify_watcher[1093675]: inOpenEvent: run cmd: /usr/bin/logger, args: [IN_OPEN,/var/tmp/dracut.log]
inotify_watcher[1093675]: inCloseEvent: path: /var/tmp, name: dracut.log, write: false
inotify_watcher[1093675]: inCloseEvent: run cmd: /usr/bin/logger, args: [IN_CLOSE_NOWRITE,/var/tmp/dracut.log]
inotify_watcher[1093675]: inOpenEvent: path: /var/tmp, name: dracut.log
inotify_watcher[1093675]: inOpenEvent: run cmd: /usr/bin/logger, args: [IN_OPEN,/var/tmp/dracut.log]
inotify_watcher[1093675]: inAccessEvent: path: /var/tmp, name: dracut.log
inotify_watcher[1093675]: inAccessEvent: run cmd: /usr/bin/logger, args: [IN_ACCESS,/var/tmp/dracut.log]
inotify_watcher[1093675]: inCloseEvent: path: /var/tmp, name: dracut.log, write: false
inotify_watcher[1093675]: inCloseEvent: run cmd: /usr/bin/logger, args: [IN_CLOSE_NOWRITE,/var/tmp/dracut.log]
inotify_watcher[1093675]: inCreateEvent: path: /var/tmp, name: .#dracut.log
inotify_watcher[1093675]: inCreateEvent: run cmd: /usr/bin/logger, args: [IN_CREATE,/var/tmp/.#dracut.log]
inotify_watcher[1093675]: inDeleteEvent: path: /var/tmp, name: .#dracut.log, self: false
inotify_watcher[1093675]: inDeleteEvent: run cmd: /usr/bin/logger, args: [IN_DELETE,/var/tmp/.#dracut.log]
inotify_watcher[1093675]: inOpenEvent: path: /var/tmp, name:
inotify_watcher[1093675]: inOpenEvent: run cmd: /usr/bin/logger, args: [IN_OPEN,/var/tmp]
inotify_watcher[1093675]: inAccessEvent: path: /var/tmp, name:
inotify_watcher[1093675]: inAccessEvent: run cmd: /usr/bin/logger, args: [IN_ACCESS,/var/tmp]
inotify_watcher[1093675]: inCloseEvent: path: /var/tmp, name: , write: false
```

## License

MIT License. See [LICENSE](LICENSE) file for details.
