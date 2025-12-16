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

// C++20 std::span support
#if __cplusplus >= 202002L
  #include <span>
#endif

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

// ============================================================================
// Span abstraction - unified interface for C++20 std::span and fallback
// ============================================================================

namespace msgq {
  
  #if __cplusplus >= 202002L
    // C++20: Use std::span directly
    template<typename T>
    using span = std::span<T>;
    
    // Mark that we're using std::span
    #define MSGQ_USING_STD_SPAN 1
    
    // Helper for creating spans with deduction
    template<typename T>
    constexpr span<T> make_span(T* data, size_t size) noexcept {
      return span<T>(data, size);
    }
    
    template<typename Container>
    constexpr auto make_span(Container& c) noexcept {
      return span<typename Container::value_type>(c);
    }
    
    template<typename Container>
    constexpr auto make_span(const Container& c) noexcept {
      return span<const typename Container::value_type>(c);
    }
  
  #elif MSGQ_HAS_GSL
    // C++17 with GSL: Use gsl::span
    using gsl::span;
    
    #undef MSGQ_USING_STD_SPAN
    
    template<typename T>
    constexpr span<T> make_span(T* data, size_t size) noexcept {
      return span<T>(data, size);
    }
    
    template<typename Container>
    constexpr auto make_span(Container& c) noexcept {
      return span<typename Container::value_type>(c);
    }
    
    template<typename Container>
    constexpr auto make_span(const Container& c) noexcept {
      return span<const typename Container::value_type>(c);
    }
  
  #else
    // C++17 fallback: Minimal span implementation
    template<typename T>
    class span {
    private:
      T* data_;
      size_t size_;
    
    public:
      // Constructors
      constexpr span() noexcept : data_(nullptr), size_(0) {}
      
      constexpr span(T* data, size_t size) noexcept 
        : data_(data), size_(size) {}
      
      // From containers (SFINAE-enabled)
      template<typename Container>
      constexpr span(Container& c) noexcept 
        : data_(c.data()), size_(c.size()) {}
      
      template<typename Container>
      constexpr span(const Container& c) noexcept 
        : data_(const_cast<T*>(c.data())), size_(c.size()) {}
      
      // Copy constructor and assignment
      constexpr span(const span&) noexcept = default;
      constexpr span& operator=(const span&) noexcept = default;
      
      // Element access
      [[nodiscard]] constexpr T* data() const noexcept { return data_; }
      [[nodiscard]] constexpr size_t size() const noexcept { return size_; }
      [[nodiscard]] constexpr size_t size_bytes() const noexcept { 
        return size_ * sizeof(T); 
      }
      [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
      
      // Indexed access
      [[nodiscard]] constexpr T& operator[](size_t idx) const noexcept {
        return data_[idx];
      }
      
      [[nodiscard]] constexpr T& front() const noexcept {
        return data_[0];
      }
      
      [[nodiscard]] constexpr T& back() const noexcept {
        return data_[size_ - 1];
      }
      
      // Iterator support
      [[nodiscard]] constexpr T* begin() const noexcept { return data_; }
      [[nodiscard]] constexpr T* end() const noexcept { return data_ + size_; }
      [[nodiscard]] constexpr const T* cbegin() const noexcept { return data_; }
      [[nodiscard]] constexpr const T* cend() const noexcept { return data_ + size_; }
      
      // Subspan
      [[nodiscard]] constexpr span subspan(size_t offset, size_t count = -1) const noexcept {
        if (count == (size_t)-1) count = size_ - offset;
        return span(data_ + offset, count);
      }
      
      [[nodiscard]] constexpr span first(size_t count) const noexcept {
        return span(data_, count);
      }
      
      [[nodiscard]] constexpr span last(size_t count) const noexcept {
        return span(data_ + size_ - count, count);
      }
      
      // Comparison
      constexpr bool operator==(const span& other) const noexcept {
        return data_ == other.data_ && size_ == other.size_;
      }
      
      constexpr bool operator!=(const span& other) const noexcept {
        return !(*this == other);
      }
    };
    
    #undef MSGQ_USING_STD_SPAN
    
    // Helper functions for span creation
    template<typename T>
    constexpr span<T> make_span(T* data, size_t size) noexcept {
      return span<T>(data, size);
    }
    
    template<typename Container>
    constexpr auto make_span(Container& c) noexcept {
      return span<typename Container::value_type>(c);
    }
    
    template<typename Container>
    constexpr auto make_span(const Container& c) noexcept {
      return span<const typename Container::value_type>(c);
    }
  
  #endif // Span implementation selection
  
} // namespace msgq

// Make gsl::span available as an alias
namespace gsl {
  using msgq::span;
}

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
    
    // Constructor for any span type (C++20 or with custom span)
    template<typename T>
    explicit Message(gsl::span<T> data) noexcept
      : data_(reinterpret_cast<const char*>(data.data()), 
              reinterpret_cast<const char*>(data.data()) + data.size() * sizeof(T)) {}
    
    // C++20 std::span constructor (only if std::span is different from msgq::span)
    #if __cplusplus >= 202002L && !defined(MSGQ_USING_STD_SPAN)
    Message(std::span<const char> data) : data_(data.begin(), data.end()) {}
    
    template<typename T>
    explicit Message(std::span<T> data) noexcept
      : data_(reinterpret_cast<const char*>(data.data()), 
              reinterpret_cast<const char*>(data.data()) + data.size() * sizeof(T)) {}
    #endif
    
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
    
    // C++20 std::span accessors
    #if __cplusplus >= 202002L
    [[nodiscard]] std::span<const char> as_span() const noexcept {
        return std::span<const char>(data_.data(), data_.size());
    }
    
    [[nodiscard]] std::span<char> as_span() noexcept {
        return std::span<char>(data_.data(), data_.size());
    }
    
    // Generic typed span access for C++20
    template<typename T>
    [[nodiscard]] std::span<const T> as_span() const noexcept {
        return std::span<const T>(
            reinterpret_cast<const T*>(data_.data()),
            data_.size() / sizeof(T)
        );
    }
    
    template<typename T>
    [[nodiscard]] std::span<T> as_span() noexcept {
        return std::span<T>(
            reinterpret_cast<T*>(data_.data()),
            data_.size() / sizeof(T)
        );
    }
    #endif
    
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
    
    // C++20 std::span overloads (only if std::span is different from msgq::span)
    #if __cplusplus >= 202002L && !defined(MSGQ_USING_STD_SPAN)
    void send(std::span<const char> data) {
      send(gsl::span<const char>(data.data(), data.size()));
    }
    
    void send(std::span<char> data) {
      send(gsl::span<const char>(data.data(), data.size()));
    }
    
    template<typename T>
    void send(std::span<T> data) requires (!std::is_same_v<T, char>) {
      send(gsl::span<const char>(
        reinterpret_cast<const char*>(data.data()), 
        data.size_bytes()
      ));
    }
    #endif
    
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
