# File Watcher Service

**File Watcher Service** is a lightweight daemon written in **C++** using the **Boost.Asio** library, designed to monitor filesystem changes through the **Linux inotify** mechanism. When file changes are detected (creation, modification, deletion), the service executes user-defined commands specified in a configuration file.

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
      "inotify": [ "IN_CLOSE_WRITE", "IN_CREATE", "IN_DELETE" ],
      "recursive": false,
      "command": "/usr/bin/true"
    },
    {
      "path": "/etc/myapp/config.ini",
      "command": "/usr/bin/true"
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

#### Default config path:
- from system `/etc/inotify_watcher/config.json`
- from environment `INOTIFY_SERVICE_CONF`
- from cmd arg `--config /path_to/config.json`

### Monitoring the service:
- set `debug` to `true` on config
- run command `journalctl -t inotify_watcher -f`

## License

MIT License. See [LICENSE](LICENSE) file for details.
