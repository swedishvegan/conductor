
// chatgpt

// unix_socket_client.cpp
//
// Robust version that only starts the python server iff it isn't already running.

#include "server.hpp"
#include "defs.hpp"

#include <chrono>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <unistd.h>

extern std::string expand_user_path(const std::string&);

#define SOCKET_PATH "/tmp/hll_socket.sock"

struct unique_fd { // Helper class
    int fd;

    unique_fd();
    explicit unique_fd(int f);
    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;
    unique_fd(unique_fd&& o) noexcept;
    unique_fd& operator=(unique_fd&& o) noexcept;
    ~unique_fd();

    int get() const;
    int release();
    void reset(int f = -1);
    explicit operator bool() const;
};

class unix_socket_client { // Helper class
public:
    explicit unix_socket_client(const std::string& socketPath);
    unix_socket_client(const unix_socket_client&) = delete;
    ~unix_socket_client() = default;

    std::string post(const std::string& data);

    bool ping() noexcept;

private:
    unique_fd sock;
    std::string socketPath;
    pid_t helper_pid = -1;

    bool recv_all(void* buf, size_t len);
    pid_t start_server();
};

std::string post(const std::string& data) {
    unix_socket_client client(SOCKET_PATH);
    return client.post(data);
}

namespace {

// ------------------------------- RAII helpers -------------------------------

inline void set_cloexec(int fd) {
    int flags = ::fcntl(fd, F_GETFD);
    if (flags != -1) ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

// EINTR-safe read/write loops
ssize_t read_full(int fd, void* buf, size_t len) {
    auto* p   = static_cast<unsigned char*>(buf);
    size_t rd = 0;
    while (rd < len) {
        ssize_t r = ::read(fd, p + rd, len - rd);
        if (r == 0) return rd; // EOF
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        rd += static_cast<size_t>(r);
    }
    return static_cast<ssize_t>(rd);
}

bool write_full(int fd, const void* buf, size_t len) {
    auto* p   = static_cast<const unsigned char*>(buf);
    size_t wr = 0;
    while (wr < len) {
        ssize_t r = ::write(fd, p + wr, len - wr);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        wr += static_cast<size_t>(r);
    }
    return true;
}

// ----------------------------------------------------------------------------

void copy_socket_path(sockaddr_un& addr, const std::string& path) {
    if (path.size() >= sizeof(addr.sun_path))
        throw std::system_error(ENAMETOOLONG, std::generic_category(),
                                "UNIX socket path too long");
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
}

// Do a single non-throwing connect attempt. Returns fd on success, -1 on error.
// out_errno is set to the errno from connect() (or from socket() if that failed).
int try_connect_once(const std::string& path, int& out_errno) {
#if defined(SOCK_CLOEXEC)
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
#endif
    if (fd == -1) {
        out_errno = errno;
        return -1;
    }
#ifndef SOCK_CLOEXEC
    set_cloexec(fd);
#endif

    sockaddr_un addr{};
    try {
        copy_socket_path(addr, path);
    } catch (...) {
        out_errno = ENAMETOOLONG;
        ::close(fd);
        return -1;
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        out_errno = 0;
        return fd;
    }

    out_errno = errno;
    ::close(fd);
    return -1;
}

// Retry connect with exponential backoff up to overall timeout. Throws on timeout or hard error.
// Only treats ENOENT/ECONNREFUSED as "keep waiting"; anything else throws immediately.
int connect_with_retry(const std::string& path, pid_t helper_pid,
                       std::chrono::milliseconds overall =
                           std::chrono::milliseconds(3000)) {
    auto deadline = std::chrono::steady_clock::now() + overall;
    int  delay_ms = 25;
    int  last_err = 0;

    auto helper_exited = [&](pid_t pid)->bool {
        if (pid <= 0) return false;
        int status = 0;
        return ::waitpid(pid, &status, WNOHANG) == pid;
    };

    while (true) {
        int fd = try_connect_once(path, last_err);
        if (fd >= 0) return fd;

        if (helper_exited(helper_pid))
            throw std::runtime_error("helper exited before creating socket");

        if (last_err != ENOENT && last_err != ECONNREFUSED && last_err != EADDRINUSE) {
            throw std::system_error(last_err, std::generic_category(), "connect");
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("timeout waiting for helper socket");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        delay_ms = std::min(delay_ms * 2, 250);
    }
}

// Acquire an exclusive flock on the given file path and run `fn`, releasing it afterwards.
template <class Fn>
void with_flock(const char* lock_path, Fn&& fn) {
#if defined(O_CLOEXEC)
    unique_fd lfd(::open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600));
#else
    unique_fd lfd(::open(lock_path, O_CREAT | O_RDWR, 0600));
    if (lfd) set_cloexec(lfd.get());
#endif
    if (!lfd) {
        throw std::system_error(errno, std::generic_category(), "open lock file");
    }

    if (::flock(lfd.get(), LOCK_EX) == -1) {
        throw std::system_error(errno, std::generic_category(), "flock");
    }

    try {
        fn();
    } catch (...) {
        ::flock(lfd.get(), LOCK_UN);
        throw;
    }
    ::flock(lfd.get(), LOCK_UN);
}

} // namespace

// --------------------------------- class impl --------------------------------

unique_fd::unique_fd() : fd(-1) {}

unique_fd::unique_fd(int f) : fd(f) {}

unique_fd::unique_fd(unique_fd&& o) noexcept : fd(o.fd) {
    o.fd = -1;
}

unique_fd& unique_fd::operator=(unique_fd&& o) noexcept {
    if (this != &o) {
        reset();
        fd = o.fd;
        o.fd = -1;
    }
    return *this;
}

unique_fd::~unique_fd() {
    reset();
}

int unique_fd::get() const {
    return fd;
}

int unique_fd::release() {
    int t = fd;
    fd = -1;
    return t;
}

void unique_fd::reset(int f) {
    if (fd >= 0)
        ::close(fd);
    fd = f;
}

unique_fd::operator bool() const {
    return fd >= 0;
}

unix_socket_client::unix_socket_client(const std::string& socketPath)
    : sock(-1), socketPath(socketPath) {
    // 1) Try a quick connect first (no server spawn path should be fast).
    int first_err = 0;
    int fd = try_connect_once(socketPath, first_err);
    if (fd >= 0) {
        sock.reset(fd);
        return;
    }

    // 2) If it failed for a *hard* reason, bail out immediately.
    if (first_err != ENOENT && first_err != ECONNREFUSED) {
        throw std::system_error(first_err, std::generic_category(), "connect");
    }

    // 3) Otherwise, try to be the one who starts the server under a lock.
    const char* lock_path = "/tmp/hll_socket.lock";
    bool        spawned   = false;

    with_flock(lock_path, [&] {
        // Re-check after acquiring the lock: maybe someone else won the race.
        int re_err = 0;
        int re_fd  = try_connect_once(socketPath, re_err);
        if (re_fd >= 0) {
            sock.reset(re_fd);
            return;
        }
        if (re_err != ENOENT && re_err != ECONNREFUSED) {
            throw std::system_error(re_err, std::generic_category(), "connect");
        }

        // We're still the first; start the server.
        helper_pid = start_server();
        spawned = true;
    });

    // 4) Now wait (with backoff) for the server to come up.
    //    Give it a bit more time if we were the one spawning it.
    const char* env_timeout = ::getenv("HLL_START_TIMEOUT_MS");
    auto wait_ms = spawned
        ? std::chrono::milliseconds(std::stoi(env_timeout ? env_timeout : "120000"))
        : std::chrono::milliseconds(2000);


    sock.reset(connect_with_retry(socketPath, helper_pid, wait_ms));
}

bool unix_socket_client::recv_all(void* buf, size_t len) {
    ssize_t r = read_full(sock.get(), buf, len);
    return r == static_cast<ssize_t>(len);
}

std::string unix_socket_client::post(const std::string& data) {
    // Length-prefixed message
    uint32_t len = htonl(static_cast<uint32_t>(data.size()));
    if (!write_full(sock.get(), &len, sizeof(len))) {
        throw std::runtime_error("Failed to send data length");
    }
    if (!write_full(sock.get(), data.data(), data.size())) {
        throw std::runtime_error("Failed to send data payload");
    }

    uint32_t resp_len_n = 0;
    if (!recv_all(&resp_len_n, sizeof(resp_len_n))) {
        throw std::runtime_error("Failed to read response length");
    }
    uint32_t resp_len = ntohl(resp_len_n);

    std::string out(resp_len, '\0');
    if (!recv_all(out.data(), resp_len)) {
        throw std::runtime_error("Failed to read response payload");
    }
    return out;
}

bool unix_socket_client::ping() noexcept {
    try {
        // send() with zero bytes; returns 0 on success, -1 on error
        if (::send(sock.get(), "", 0, MSG_NOSIGNAL) == -1)
            throw std::system_error(errno, std::generic_category(), "ping");
        return true;
    } catch (const std::system_error&) {
        return false;
    } catch (const std::exception&) {
        return false;
    }
}

pid_t unix_socket_client::start_server() {

    pid_t pid = ::fork();
    if (pid == -1) {
        throw std::runtime_error("fork() failed");
    }
    if (pid == 0) {
        // --- Child process ---
        ::setsid();  // New session, detach from terminal

        // Open a logfile for stdout/stderr
        std::string log_path = expand_user_path(hll_projects_folder "/hll_server.log");
        int logfd = ::open(log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (logfd >= 0) {
            ::dup2(logfd, STDOUT_FILENO);
            ::dup2(logfd, STDERR_FILENO);
            if (logfd > 2) ::close(logfd);
        }

        // Redirect stdin to /dev/null
        int devnull = ::open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
            if (devnull > 2) ::close(devnull);
        }

        const std::string script = expand_user_path(hll_projects_folder "/server.py");
        ::execlp("python3", "python3", "-u", script.c_str(), (char*)nullptr);

        // If execlp fails:
        int e = errno;
        dprintf(STDERR_FILENO, "execlp failed to start server (%d): %s\n", e, std::strerror(e));
        _exit(127);
    }
    // --- Parent just returns ---
    return pid;
}

void kill_server()
{
    const char* pidfile = "/tmp/hll_server.pid";
    const char* lock    = "/tmp/hll_socket.lock";

    with_flock(lock, [&] {
        std::ifstream in(pidfile);
        if (!in) {
            std::cout << "No running server found.\n";
            return;
        }
        pid_t pid;
        in >> pid;
        if (pid <= 0) {
            std::cerr << "PID file corrupt; deleting.\n";
            std::remove(pidfile);
            return;
        }

        // Is the process really alive?
        if (::kill(pid, 0) == -1 && errno == ESRCH) {
            std::cout << "Stale PID file; deleting.\n";
            std::remove(pidfile);
            return;
        }

        std::cout << "Sending SIGKILL to server (" << pid << ")…\n";
        ::kill(pid, SIGKILL);

        std::remove(pidfile);
        std::remove(SOCKET_PATH);              // tidy up the stale socket
        std::cout << "Server stopped.\n";
    });
}
