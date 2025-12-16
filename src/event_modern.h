#pragma once

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <filesystem>

#define CEREAL_EVENTS_PREFIX std::string("cereal_events")

namespace msgq::event {

// ============================================================================
// RAII 守卫类
// ============================================================================

// FD 守卫 - 自动关闭文件描述符
class FdGuard {
private:
    int fd_ = -1;

    void close_fd() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

public:
    FdGuard() = default;
    
    explicit FdGuard(int fd) noexcept : fd_(fd) {}
    
    ~FdGuard() { close_fd(); }

    // 禁止复制
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    // 允许移动
    FdGuard(FdGuard&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this != &other) {
            close_fd();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

    // 释放所有权
    int release() noexcept {
        int result = fd_;
        fd_ = -1;
        return result;
    }
};

// Mmap 守卫 - 自动 munmap
class MmapGuard {
private:
    void* addr_ = nullptr;
    size_t size_ = 0;

    void unmap() noexcept {
        if (addr_ != nullptr && addr_ != reinterpret_cast<void*>(-1)) {
            ::munmap(addr_, size_);
        }
    }

public:
    MmapGuard() = default;

    MmapGuard(void* addr, size_t size) noexcept : addr_(addr), size_(size) {
        if (addr_ == MAP_FAILED) {
            addr_ = nullptr;
            size_ = 0;
        }
    }

    ~MmapGuard() { unmap(); }

    // 禁止复制
    MmapGuard(const MmapGuard&) = delete;
    MmapGuard& operator=(const MmapGuard&) = delete;

    // 允许移动
    MmapGuard(MmapGuard&& other) noexcept 
        : addr_(other.addr_), size_(other.size_) {
        other.addr_ = nullptr;
        other.size_ = 0;
    }

    MmapGuard& operator=(MmapGuard&& other) noexcept {
        if (this != &other) {
            unmap();
            addr_ = other.addr_;
            size_ = other.size_;
            other.addr_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    [[nodiscard]] void* get() const noexcept { return addr_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool valid() const noexcept { 
        return addr_ != nullptr && addr_ != MAP_FAILED; 
    }
    explicit operator bool() const noexcept { return valid(); }

    // 释放所有权
    void* release() noexcept {
        void* result = addr_;
        addr_ = nullptr;
        size_ = 0;
        return result;
    }
};

// Eventfd 守卫 - 自动关闭 eventfd
class EventfdGuard {
private:
    int fd_ = -1;

    void close_fd() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

public:
    EventfdGuard() = default;

    explicit EventfdGuard(int fd) noexcept : fd_(fd) {}

    ~EventfdGuard() { close_fd(); }

    // 禁止复制
    EventfdGuard(const EventfdGuard&) = delete;
    EventfdGuard& operator=(const EventfdGuard&) = delete;

    // 允许移动
    EventfdGuard(EventfdGuard&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    EventfdGuard& operator=(EventfdGuard&& other) noexcept {
        if (this != &other) {
            close_fd();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

    int release() noexcept {
        int result = fd_;
        fd_ = -1;
        return result;
    }
};

// ============================================================================
// 事件状态结构
// ============================================================================

enum class EventPurpose : int {
    RECV_CALLED = 0,
    RECV_READY = 1
};

struct EventState {
    int fds[2];        // [RECV_CALLED, RECV_READY]
    bool enabled;
    
    EventState() : fds{-1, -1}, enabled(false) {}
};

// ============================================================================
// 事件类（RAII 包装）
// ============================================================================

class Event {
private:
    int event_fd_ = -1;

    void validate() const {
        if (event_fd_ < 0) {
            throw std::runtime_error("Event does not have valid file descriptor");
        }
    }

public:
    // 构造
    Event() = default;
    
    explicit Event(int fd) noexcept : event_fd_(fd) {}

    // 析构
    ~Event() = default;

    // 不可复制，可移动
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&& other) noexcept : event_fd_(other.event_fd_) {
        other.event_fd_ = -1;
    }
    Event& operator=(Event&& other) noexcept {
        if (this != &other) {
            event_fd_ = other.event_fd_;
            other.event_fd_ = -1;
        }
        return *this;
    }

    // 操作
    void set() const {
        validate();
        uint64_t val = 1;
        if (::write(event_fd_, &val, sizeof(uint64_t)) < 0) {
            throw std::runtime_error("Failed to set event: " + std::string(strerror(errno)));
        }
    }

    int clear() const {
        validate();
        uint64_t val = 0;
        if (::read(event_fd_, &val, sizeof(uint64_t)) < 0) {
            throw std::runtime_error("Failed to clear event: " + std::string(strerror(errno)));
        }
        return static_cast<int>(val);
    }

    void wait(int timeout_sec = -1) const {
        validate();

        struct pollfd fds = {event_fd_, POLLIN, 0};
        struct timespec timeout = {timeout_sec, 0};

        sigset_t signals;
        ::sigfillset(&signals);
        ::sigdelset(&signals, SIGALRM);
        ::sigdelset(&signals, SIGINT);
        ::sigdelset(&signals, SIGTERM);
        ::sigdelset(&signals, SIGQUIT);

        int event_count = ::ppoll(&fds, 1, timeout_sec < 0 ? nullptr : &timeout, &signals);

        if (event_count == 0) {
            throw std::runtime_error("Event timed out (pid: " + std::to_string(::getpid()) + ")");
        } else if (event_count < 0) {
            throw std::runtime_error("Event poll failed: " + std::string(strerror(errno)) + 
                                   " (pid: " + std::to_string(::getpid()) + ")");
        }
    }

    [[nodiscard]] bool peek() const noexcept {
        if (event_fd_ < 0) return false;

        struct pollfd fds = {event_fd_, POLLIN, 0};
        int event_count = ::poll(&fds, 1, 0);
        return event_count != 0;
    }

    [[nodiscard]] bool is_valid() const noexcept {
        return event_fd_ >= 0;
    }

    [[nodiscard]] int fd() const noexcept {
        return event_fd_;
    }

    // 等待多个事件中的任意一个
    static int wait_for_one(const std::vector<Event>& events, int timeout_sec = -1) {
        if (events.empty()) {
            throw std::invalid_argument("No events to wait for");
        }

        // 使用标准容器而非 VLA
        std::vector<struct pollfd> fds;
        fds.reserve(events.size());
        
        for (const auto& event : events) {
            if (event.is_valid()) {
                fds.push_back({event.fd(), POLLIN, 0});
            }
        }

        if (fds.empty()) {
            throw std::runtime_error("All events are invalid");
        }

        struct timespec timeout = {timeout_sec, 0};

        sigset_t signals;
        ::sigfillset(&signals);
        ::sigdelset(&signals, SIGALRM);
        ::sigdelset(&signals, SIGINT);
        ::sigdelset(&signals, SIGTERM);
        ::sigdelset(&signals, SIGQUIT);

        int event_count = ::ppoll(fds.data(), fds.size(), 
                                 timeout_sec < 0 ? nullptr : &timeout, &signals);

        if (event_count == 0) {
            throw std::runtime_error("Event poll timed out (pid: " + std::to_string(::getpid()) + ")");
        } else if (event_count < 0) {
            throw std::runtime_error("Event poll failed: " + std::string(strerror(errno)) + 
                                   " (pid: " + std::to_string(::getpid()) + ")");
        }

        for (size_t i = 0; i < fds.size(); i++) {
            if (fds[i].revents & POLLIN) {
                return static_cast<int>(i);
            }
        }

        throw std::runtime_error("No events ready after poll returned");
    }
};

// ============================================================================
// 事件句柄类（管理共享内存中的事件对）
// ============================================================================

class SocketEventHandle {
private:
    std::string shm_path_;
    MmapGuard mmap_;
    EventState* state_ = nullptr;

public:
    // 创建事件处理器
    SocketEventHandle(const std::string& endpoint, const std::string& identifier = "", 
                     bool override = true) {
        void* mem = nullptr;
        std::string path;
        
        try {
            // 映射共享内存
            map_event_state(endpoint, identifier, mem, path);
            
            state_ = static_cast<EventState*>(mem);
            shm_path_ = path;
            mmap_ = MmapGuard(mem, sizeof(EventState));

            if (override) {
                // 创建 eventfd，使用 RAII 守卫管理
                EventfdGuard fd0(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
                EventfdGuard fd1(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));

                if (!fd0.valid() || !fd1.valid()) {
                    throw std::runtime_error("Failed to create eventfds");
                }

                state_->fds[static_cast<int>(EventPurpose::RECV_CALLED)] = fd0.release();
                state_->fds[static_cast<int>(EventPurpose::RECV_READY)] = fd1.release();
            }
        } catch (const std::exception&) {
            // 异常安全：RAII 自动清理
            throw;
        }
    }

    // 析构 - 自动清理
    ~SocketEventHandle() {
        if (state_ != nullptr && mmap_.valid()) {
            // 关闭 eventfd
            if (state_->fds[0] >= 0) {
                ::close(state_->fds[0]);
            }
            if (state_->fds[1] >= 0) {
                ::close(state_->fds[1]);
            }
            
            // munmap 和 unlink 由 MmapGuard 和 filesystem 处理
            if (!shm_path_.empty()) {
                ::unlink(shm_path_.c_str());
            }
        }
    }

    // 非可复制，可移动
    SocketEventHandle(const SocketEventHandle&) = delete;
    SocketEventHandle& operator=(const SocketEventHandle&) = delete;
    
    SocketEventHandle(SocketEventHandle&& other) noexcept
        : shm_path_(std::move(other.shm_path_)),
          mmap_(std::move(other.mmap_)),
          state_(other.state_) {
        other.state_ = nullptr;
    }

    SocketEventHandle& operator=(SocketEventHandle&& other) noexcept {
        if (this != &other) {
            // 析构当前对象
            this->~SocketEventHandle();
            
            shm_path_ = std::move(other.shm_path_);
            mmap_ = std::move(other.mmap_);
            state_ = other.state_;
            other.state_ = nullptr;
        }
        return *this;
    }

    // 查询/修改启用状态
    [[nodiscard]] bool is_enabled() const {
        if (state_ == nullptr) throw std::runtime_error("SocketEventHandle not initialized");
        return state_->enabled;
    }

    void set_enabled(bool enabled) {
        if (state_ == nullptr) throw std::runtime_error("SocketEventHandle not initialized");
        state_->enabled = enabled;
    }

    // 获取事件对
    [[nodiscard]] Event recv_called() const {
        if (state_ == nullptr) throw std::runtime_error("SocketEventHandle not initialized");
        int fd = state_->fds[static_cast<int>(EventPurpose::RECV_CALLED)];
        if (fd < 0) throw std::runtime_error("recv_called event not initialized");
        return Event(fd);
    }

    [[nodiscard]] Event recv_ready() const {
        if (state_ == nullptr) throw std::runtime_error("SocketEventHandle not initialized");
        int fd = state_->fds[static_cast<int>(EventPurpose::RECV_READY)];
        if (fd < 0) throw std::runtime_error("recv_ready event not initialized");
        return Event(fd);
    }

    // 全局虚假事件控制
    static void toggle_fake_events(bool enabled) {
        if (enabled) {
            ::setenv("CEREAL_FAKE", "1", true);
        } else {
            ::unsetenv("CEREAL_FAKE");
        }
        fake_events_enabled_ = enabled;
    }

    static void set_fake_prefix(const std::string& prefix) {
        if (prefix.empty()) {
            ::unsetenv("CEREAL_FAKE_PREFIX");
        } else {
            ::setenv("CEREAL_FAKE_PREFIX", prefix.c_str(), true);
        }
        fake_prefix_ = prefix;
    }

    static std::string fake_prefix() {
        const char* prefix = std::getenv("CEREAL_FAKE_PREFIX");
        return prefix != nullptr ? std::string(prefix) : "";
    }

    static void map_event_state(const std::string& endpoint, 
                               const std::string& identifier,
                               void*& shm_mem,
                               std::string& shm_path) {
        const char* op_prefix = std::getenv("OPENPILOT_PREFIX");

        std::string full_path = "/dev/shm/";
        if (op_prefix) {
            full_path += std::string(op_prefix) + "/";
        }
        full_path += CEREAL_EVENTS_PREFIX + "/";
        if (!identifier.empty()) {
            full_path += identifier + "/";
        }

        // 创建目录
        try {
            std::filesystem::create_directories(full_path);
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to create directories: " + std::string(e.what()));
        }

        full_path += endpoint;

        // 打开共享内存文件
        FdGuard shm_fd(::open(full_path.c_str(), O_RDWR | O_CREAT, 0664));
        if (!shm_fd.valid()) {
            throw std::runtime_error("Could not open shared memory file: " + 
                                   std::string(strerror(errno)));
        }

        // 截断到所需大小
        if (::ftruncate(shm_fd.get(), sizeof(EventState)) < 0) {
            throw std::runtime_error("Could not truncate shared memory file: " + 
                                   std::string(strerror(errno)));
        }

        // 映射到内存
        void* mem = ::mmap(nullptr, sizeof(EventState), 
                          PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd.get(), 0);
        
        if (mem == MAP_FAILED) {
            throw std::runtime_error("Could not map shared memory file: " + 
                                   std::string(strerror(errno)));
        }

        shm_mem = mem;
        shm_path = full_path;
    }

public:
    static thread_local bool fake_events_enabled_;
    static thread_local std::string fake_prefix_;
};

} // namespace msgq::event
