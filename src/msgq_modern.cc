#include "msgq_modern.h"
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <memory>

namespace msgq {

// ============================================================================
// RAII Guard implementations
// ============================================================================

void MmapGuard::cleanup() noexcept {
    if (valid()) {
        ::munmap(addr_, size_);
        addr_ = nullptr;
        size_ = 0;
    }
}

void FdGuard::cleanup() noexcept {
    if (valid()) {
        ::close(fd_);
        fd_ = -1;
    }
}

// ============================================================================
// Low-level queue implementation (opaque to user)
// ============================================================================

class Queue::Impl {
public:
    struct Header {
        std::atomic<uint64_t> write_index;
        std::atomic<uint64_t> read_index[NUM_READERS];
        uint32_t num_readers;
        uint32_t reader_uid;
        uint64_t segment_size;
    };

    // Shared memory management
    FdGuard fd_;
    MmapGuard mmap_;
    Header* header_ = nullptr;
    char* data_start_ = nullptr;
    std::string name_;
    size_t size_;
    int reader_id_ = -1;
    bool is_publisher_ = false;

    Impl(std::string_view name, size_t size) 
        : name_(name), size_(align_to_8(size)) {
        init_shared_memory();
    }

    ~Impl() {
        // Cleanup happens through guard destructors
    }

    void init_shared_memory() {
        // Create or open shared memory object
        std::string shm_path = "/dev/shm/" + std::string(name_);
        
        FdGuard fd(::open(shm_path.c_str(), O_CREAT | O_RDWR, 0666));
        if (!fd.valid()) {
            throw MessageQueueError("Failed to open shared memory: " + std::string(strerror(errno)));
        }

        // Resize to fit header + data
        size_t total_size = sizeof(Header) + size_;
        if (::ftruncate(fd.get(), total_size) < 0) {
            throw MessageQueueError("Failed to truncate shared memory");
        }

        // Map into memory
        void* addr = ::mmap(nullptr, total_size, PROT_READ | PROT_WRITE, 
                           MAP_SHARED, fd.get(), 0);
        if (addr == MAP_FAILED) {
            throw MessageQueueError("Failed to mmap shared memory");
        }

        // Initialize guards
        fd_ = std::move(fd);
        mmap_ = MmapGuard(addr, total_size);
        
        // Setup pointers
        header_ = static_cast<Header*>(addr);
        data_start_ = static_cast<char*>(addr) + sizeof(Header);
    }

    void send_message(gsl::span<const char> data) {
        if (!is_publisher_) {
            throw MessageQueueError("Not initialized as publisher");
        }
        if (data.size() > size_) {
            throw MessageQueueError("Message too large for queue");
        }

        // Write to circular buffer (simplified - actual implementation needs lock-free logic)
        PackedPointer write_ptr(
            header_->write_index.load(std::memory_order_acquire)
        );
        uint32_t offset = write_ptr.offset();
        
        // Copy data
        if (offset + data.size() > size_) {
            // Wrap around
            memcpy(data_start_ + offset, data.data(), size_ - offset);
            memcpy(data_start_, data.data() + (size_ - offset), 
                   data.size() - (size_ - offset));
        } else {
            memcpy(data_start_ + offset, data.data(), data.size());
        }

        // Update write pointer with new cycle counter
        PackedPointer new_ptr(write_ptr.cycle() + 1, 
                            (offset + data.size()) % size_);
        header_->write_index.store(new_ptr.raw(), std::memory_order_release);
    }

    Message receive_message(int timeout_ms, bool conflate) {
        if (reader_id_ < 0) {
            throw MessageQueueError("Not initialized as subscriber");
        }

        // Simplified: actual implementation needs proper synchronization
        PackedPointer read_ptr(
            header_->read_index[reader_id_].load(std::memory_order_acquire)
        );
        PackedPointer write_ptr(
            header_->write_index.load(std::memory_order_acquire)
        );

        if (read_ptr == write_ptr) {
            return Message();  // No new data
        }

        // Extract message size (simplified - needs proper framing)
        // In real implementation, this would read from message header at offset
        Message result;
        
        // Update read pointer
        header_->read_index[reader_id_].store(write_ptr.raw(), 
                                             std::memory_order_release);
        return result;
    }
};

// ============================================================================
// Queue public interface
// ============================================================================

Queue Queue::create(std::string_view name, size_t size) {
    auto impl = std::make_unique<Impl>(name, size);
    return Queue(std::move(impl));
}

void Queue::send(gsl::span<const char> data) {
    if (!impl_) throw MessageQueueError("Queue not initialized");
    impl_->send_message(data);
}

void Queue::send(const Message& msg) {
    if (!impl_) throw MessageQueueError("Queue not initialized");
    impl_->send_message(msg.data());
}

Message Queue::recv(int timeout_ms, bool conflate) {
    if (!impl_) throw MessageQueueError("Queue not initialized");
    return impl_->receive_message(timeout_ms, conflate);
}

bool Queue::msg_ready() const {
    if (!impl_) return false;
    auto read_ptr = impl_->header_->read_index[impl_->reader_id_].load(
        std::memory_order_acquire
    );
    auto write_ptr = impl_->header_->write_index.load(
        std::memory_order_acquire
    );
    return PackedPointer(read_ptr) != PackedPointer(write_ptr);
}

void Queue::init_publisher() {
    if (!impl_) throw MessageQueueError("Queue not initialized");
    impl_->is_publisher_ = true;
}

void Queue::init_subscriber(bool conflate) {
    if (!impl_) throw MessageQueueError("Queue not initialized");
    
    // Assign reader ID
    uint32_t reader_uid = static_cast<uint32_t>(getpid()) << 16;
    impl_->reader_id_ = impl_->header_->num_readers++;
    
    if (impl_->reader_id_ >= NUM_READERS) {
        throw MessageQueueError("Maximum number of subscribers reached");
    }
}

size_t Queue::num_readers() const {
    if (!impl_) return 0;
    return impl_->header_->num_readers;
}

bool Queue::all_readers_updated() const {
    if (!impl_) return false;
    
    auto write_ptr = impl_->header_->write_index.load(
        std::memory_order_acquire
    );
    
    for (size_t i = 0; i < impl_->header_->num_readers; ++i) {
        auto read_ptr = impl_->header_->read_index[i].load(
            std::memory_order_acquire
        );
        if (read_ptr != write_ptr) {
            return false;
        }
    }
    return true;
}

std::string_view Queue::name() const {
    if (!impl_) return "";
    return impl_->name_;
}

void* Queue::raw_handle() noexcept {
    return impl_.get();
}

const void* Queue::raw_handle() const noexcept {
    return impl_.get();
}

} // namespace msgq

// Queue implementation - constructors and destructors
namespace msgq {

Queue::Queue(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Queue::~Queue() = default;

Queue::Queue(Queue&&) noexcept = default;
Queue& Queue::operator=(Queue&&) noexcept = default;
} // namespace msgq
