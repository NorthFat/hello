#pragma once

/*
 * Modern C++ wrapper for lock-free single producer multi-consumer message queue
 * This header provides RAII-based, type-safe abstractions over the low-level msgq implementation
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <stdexcept>

// Optional: GSL support
// Install: sudo apt-get install gsl-lite-dev (Ubuntu/Debian)
//          brew install gsl (macOS)
#ifdef __has_include
  #if __has_include(<gsl/gsl>)
    #include <gsl/gsl>
    #define MSGQ_HAS_GSL 1
  #else
    #define MSGQ_HAS_GSL 0
  #endif
#else
  #define MSGQ_HAS_GSL 0
#endif

// Simple span replacement if GSL not available
namespace msgq {
  #if MSGQ_HAS_GSL
    using gsl::span;
  #else
    // Minimal span implementation for when GSL is not available
    template<typename T>
    class span {
    private:
      T* data_;
      size_t size_;
    public:
      constexpr span() noexcept : data_(nullptr), size_(0) {}
      constexpr span(T* data, size_t size) noexcept : data_(data), size_(size) {}
      template<typename Container>
      constexpr span(Container& c) noexcept 
        : data_(c.data()), size_(c.size()) {}
      template<typename Container>
      constexpr span(const Container& c) noexcept 
        : data_(const_cast<T*>(c.data())), size_(c.size()) {}
      
      constexpr T* data() const noexcept { return data_; }
      constexpr size_t size() const noexcept { return size_; }
      constexpr bool empty() const noexcept { return size_ == 0; }
      
      constexpr T* begin() const noexcept { return data_; }
      constexpr T* end() const noexcept { return data_ + size_; }
    };
  #endif
} // namespace msgq

namespace gsl = msgq;

namespace msgq {

// ============================================================================
// Compile-time constants and utilities
// ============================================================================

constexpr size_t DEFAULT_SEGMENT_SIZE = 10 * 1024 * 1024;
constexpr size_t NUM_READERS = 15;
constexpr size_t DEFAULT_TIMEOUT_MS = 100;

// Alignment helper
constexpr size_t align_to_8(size_t n) noexcept {
    return (n + 7) & ~7ULL;
}

// Bit packing/unpacking for cycle counter + pointer
class PackedPointer {
    uint64_t value_;

public:
    constexpr PackedPointer() noexcept : value_(0) {}
    
    explicit constexpr PackedPointer(uint64_t raw) noexcept : value_(raw) {}
    
    constexpr PackedPointer(uint32_t cycle, uint32_t offset) noexcept
        : value_((static_cast<uint64_t>(cycle) << 32) | offset) {}
    
    [[nodiscard]] constexpr uint32_t cycle() const noexcept {
        return static_cast<uint32_t>(value_ >> 32);
    }
    
    [[nodiscard]] constexpr uint32_t offset() const noexcept {
        return static_cast<uint32_t>(value_ & 0xFFFFFFFFULL);
    }
    
    [[nodiscard]] constexpr uint64_t raw() const noexcept { return value_; }
    
    constexpr bool operator==(const PackedPointer& other) const noexcept {
        return value_ == other.value_;
    }
    
    constexpr bool operator!=(const PackedPointer& other) const noexcept {
        return value_ != other.value_;
    }
};

// ============================================================================
// Message buffer - Modern C++ container
// ============================================================================

class Message {
private:
    std::vector<char> data_;

public:
    // Constructors
    Message() = default;
    
    explicit Message(size_t size) : data_(size) {}
    
    Message(gsl::span<const char> data) : data_(data.begin(), data.end()) {}
    
    template<typename Iterator>
    Message(Iterator begin, Iterator end) : data_(begin, end) {}
    
    // Rule of Five - use defaults (std::vector handles everything)
    Message(const Message&) = default;
    Message& operator=(const Message&) = default;
    Message(Message&&) noexcept = default;
    Message& operator=(Message&&) noexcept = default;
    ~Message() = default;
    
    // Accessors
    [[nodiscard]] gsl::span<const char> data() const noexcept {
        return gsl::span<const char>(data_.data(), data_.size());
    }
    
    [[nodiscard]] gsl::span<char> data() noexcept {
        return gsl::span<char>(data_.data(), data_.size());
    }
    
    [[nodiscard]] size_t size() const noexcept { return data_.size(); }
    
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    
    // Resize
    void resize(size_t new_size) { data_.resize(new_size); }
    
    void clear() noexcept { data_.clear(); }
    
    // Raw access for compatibility
    [[nodiscard]] char* data_ptr() noexcept { return data_.data(); }
    [[nodiscard]] const char* data_ptr() const noexcept { return data_.data(); }
};

// ============================================================================
// RAII Wrappers for file descriptors and memory maps
// ============================================================================

class MmapGuard {
private:
    void* addr_ = nullptr;
    size_t size_ = 0;
    
public:
    MmapGuard() = default;
    
    MmapGuard(void* addr, size_t size) noexcept : addr_(addr), size_(size) {}
    
    // Non-copyable
    MmapGuard(const MmapGuard&) = delete;
    MmapGuard& operator=(const MmapGuard&) = delete;
    
    // Moveable
    MmapGuard(MmapGuard&& other) noexcept 
        : addr_(other.addr_), size_(other.size_) {
        other.addr_ = nullptr;
        other.size_ = 0;
    }
    
    MmapGuard& operator=(MmapGuard&& other) noexcept {
        if (this != &other) {
            cleanup();
            addr_ = other.addr_;
            size_ = other.size_;
            other.addr_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }
    
    ~MmapGuard() { cleanup(); }
    
    [[nodiscard]] void* get() const noexcept { return addr_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool valid() const noexcept { 
        return addr_ != nullptr && addr_ != (void*)(-1); 
    }
    
    explicit operator bool() const noexcept { return valid(); }
    
private:
    void cleanup() noexcept;
};

class FdGuard {
private:
    int fd_ = -1;
    
public:
    FdGuard() = default;
    
    explicit FdGuard(int fd) noexcept : fd_(fd) {}
    
    // Non-copyable
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
    
    // Moveable
    FdGuard(FdGuard&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    
    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this != &other) {
            cleanup();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    
    ~FdGuard() { cleanup(); }
    
    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }
    
private:
    void cleanup() noexcept;
};

// ============================================================================
// Queue - Thread-safe lock-free queue wrapper
// ============================================================================

class Queue {
private:
    // Internal low-level queue (opaque handle)
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    // Private constructor for factory
    explicit Queue(std::unique_ptr<Impl> impl);
    
public:
    // Non-copyable, moveable
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&&) noexcept;
    Queue& operator=(Queue&&) noexcept;
    ~Queue();
    
    // Factory methods
    [[nodiscard]] static Queue create(std::string_view name, size_t size = DEFAULT_SEGMENT_SIZE);
    
    // Send message (single producer)
    void send(gsl::span<const char> data);
    void send(const Message& msg);
    
    // Receive message (multiple consumers)
    [[nodiscard]] Message recv(int timeout_ms = DEFAULT_TIMEOUT_MS, bool conflate = false);
    [[nodiscard]] bool msg_ready() const;
    
    // Publisher control
    void init_publisher();
    void init_subscriber(bool conflate = false);
    
    // Status queries
    [[nodiscard]] size_t num_readers() const;
    [[nodiscard]] bool all_readers_updated() const;
    [[nodiscard]] std::string_view name() const;
    
    // For low-level access if needed
    [[nodiscard]] void* raw_handle() noexcept;
    [[nodiscard]] const void* raw_handle() const noexcept;
};

// ============================================================================
// Error handling
// ============================================================================

class MessageQueueError : public std::runtime_error {
public:
    explicit MessageQueueError(const std::string& msg) 
        : std::runtime_error(msg) {}
};

// ============================================================================
// Backward compatibility layer - C-style API wrappers
// ============================================================================

// Old-style initialization functions (for testing and gradual migration)
namespace legacy {
    // These map to the original C functions
    // They should be avoided in new code
}

} // namespace msgq

// ============================================================================
// Concept checks (C++20)
// ============================================================================

#if __cplusplus >= 202002L

namespace msgq {

template<typename T>
concept Sendable = requires(const T& t) {
    { t.data() } -> std::convertible_to<gsl::span<const char>>;
};

} // namespace msgq

#endif // MSGQ_MODERN_H_
