*This project has been created as part of the 42 curriculum by taewonki, juyoukim, jaemyu.*

> 한국어 버전 / Korean version: [README_KO.md](./README_KO.md)

# 42_Webserv

An HTTP/1.1 web server written in C++98.

## Description

42 Webserv project implementation. Reads an nginx-style configuration file and handles many concurrent connections from a single process via an `epoll`-based event loop. Supports GET / POST / DELETE, static file serving, file uploads, CGI (Python/PHP) execution, keep-alive, `error_page` mapping, `client_max_body_size`, and chunked Transfer-Encoding terminator detection.

### Goals

- Reproduce nginx-like routing / method restriction / CGI delegation in a single-process event loop
- Comply with HTTP/1.1 core (RFC 7230 / 7231)
- Maintain high availability under load (siege) — currently 100% under static GET with `Connection: close`

## Instructions

### Build

```bash
make            # build the webserv binary
make re         # full rebuild
make clean      # remove object files
make fclean     # remove objects + binary
```

Requires `c++` (g++ or clang++) and GNU make. No external library dependencies. Compiler flags: `-Wall -Wextra -Werror -std=c++98`.

### Run

```bash
./webserv                       # uses conf/default.conf
./webserv conf/<your>.conf      # uses the specified conf
```

The default conf listens on `0.0.0.0:8080`. Stop with `Ctrl-C`.

### Sanity checks

```bash
curl http://localhost:8080/                                   # 200, static index
curl http://localhost:8080/cgi-bin/hello.py                   # 200, CGI
curl -X POST -F file=@<path> http://localhost:8080/uploads/   # 201, upload
curl -X DELETE http://localhost:8080/uploads/<filename>       # 200, delete
```

In a browser:
- `/` — route navigation
- `/uploads.html` — upload / list / delete test UI
- `/error.html` — error page preview (400/403/404/405/413/5xx)

## Features

- HTTP/1.1 methods: **GET / POST / DELETE**
- Static file serving + directory default index
- File upload (`multipart/form-data`)
- CGI (`.py` / `.php`) — asynchronous pipe I/O
- keep-alive support
- `error_page` mapping (per ServerBlock)
- `client_max_body_size` enforcement (per ServerBlock)
- `Content-Length` / `Transfer-Encoding: chunked` terminator detection
- Multiple ServerBlocks + multiple `listen` (host:port)
- Virtual hosting via `server_name`
- Client idle / CGI execution timeouts (60s / 30s) with automatic cleanup
- Automatic CGI zombie / orphan reaping
- Forced `Connection: close` on error responses
- RFC 7230 §3.3.3 violations (duplicate `Content-Length`, `Content-Length` + `Transfer-Encoding` coexistence) → 400 reject

## Architecture / Team Roles

Three areas, split across the team.

| Area | Owner | Main classes |
|---|---|---|
| Config parsing | **jaemyu** | `Config`, `ConfigParser`, `ServerBlock`, `Location` |
| Main loop / event dispatch / network | **taewonki** | `ServerManager`, `ServerSocket`, `ClientSocket` |
| HTTP / CGI logic | **juyoukim** | `HttpRequest`, `HttpResponse`, `RequestHandler`, `Cgi` |

### Project Layout

```
.
├── conf/                # server configs (default.conf etc.)
├── includes/            # headers
├── srcs/
│   ├── main.cpp
│   ├── core/            # network / event loop  (taewonki)
│   ├── config/          # config parser         (jaemyu)
│   └── http/            # HTTP / CGI            (juyoukim)
├── cgi-bin/             # sample CGI scripts
├── www/                 # static pages + error pages
├── Makefile
└── README.md
```

## Technical Choices

- **C++98 standard** — assignment constraint. Uses STL containers and iterators but no C++11+ features (`auto`, lambdas, smart pointers)
- **epoll (Linux), Level-Triggered** — partial read/write relies on LT's automatic re-fire, avoiding the trickier ET backlog management
- **Single process / single thread** — all I/O is non-blocking, dispatched from one `epoll_wait` loop
- **Asynchronous CGI** — `fork` + `pipe`, then the pipe fds are registered with epoll. The parent returns to the main loop immediately
- **Timeouts** — idle 60s / CGI 30s. `sweepTimeouts()` runs at the end of each cycle and clears expired clients with `SIGKILL` + `waitpid` + cleanup
- **Subject rule: no `errno` after `read`/`write`/`recv`/`send`** — partial I/O and `EAGAIN` are handled by LT re-fire; real hangs are caught by the timeout

## Resources

### References

- [RFC 7230](https://datatracker.ietf.org/doc/html/rfc7230) — HTTP/1.1 Message Syntax and Routing
- [RFC 7231](https://datatracker.ietf.org/doc/html/rfc7231) — HTTP/1.1 Semantics and Content
- [RFC 3875](https://datatracker.ietf.org/doc/html/rfc3875) — The Common Gateway Interface (CGI)
- [nginx documentation](https://nginx.org/en/docs/) — for directive semantics and routing behavior
- man pages: [epoll(7)](https://man7.org/linux/man-pages/man7/epoll.7.html), [accept(2)](https://man7.org/linux/man-pages/man2/accept.2.html), [pipe(2)](https://man7.org/linux/man-pages/man2/pipe.2.html), [fork(2)](https://man7.org/linux/man-pages/man2/fork.2.html), [waitpid(2)](https://man7.org/linux/man-pages/man2/waitpid.2.html)
- [42 Webserv subject](https://cdn.intra.42.fr/pdf/pdf/154361/en.subject.pdf)
- [siege](https://www.joedog.org/siege-home/) — load testing tool

### AI usage

This project used Anthropic **Claude** (Claude Code, `claude-opus-4-7` model) for the following tasks.

- **Code review / static analysis** — audited the network layer's epoll dispatch, FD lifecycle, and CGI process reaping flow; prioritized findings for follow-up
- **Load test interpretation** — traced root causes of siege failures (notably the CGI c≥5 SIGSEGV) and abnormal log patterns such as `[response] sent 0 bytes`
- **Regression validation** — measured before/after for each patch on identical scenarios
- **Documentation support** — composed status, priority, and siege result tables
- **Evaluation assets** — wrote helper static pages (`/uploads.html`, `/error.html`) and `cgi-bin/list_uploads.py` (workaround for missing autoindex)
- **Cross-area review** — when a fix in the network area required changes outside the owned scope, assessed the impact and shared with the responsible teammate

Core design decisions (state machine, module split, message parsing strategy) were made by humans. All AI-generated code was reviewed, built, and tested by humans before merging.

## Code Convention

- Variable names: `snake_case`
- Function names: `camelCase` (getters/setters share names with the member they expose)
- Constructor / setter parameters: same name as the corresponding member
- Single-line functions (typically getters) stay on one line
- The constructor only constructs the object; initial-value assignment lives in a dedicated `init()` method (so an exception during construction does not skip the destructor)
- **RAII**: each member is released by its owning class's destructor; releasing a member from another class is forbidden
