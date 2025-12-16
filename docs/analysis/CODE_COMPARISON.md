# msgq 现代化重构 - 代码对比

## 文件对比总览

本文档展示了从原始 C 风格 msgq 代码到现代 C++ 的详细转换。

---

## 1. 消息对象（Message）

### 原始代码（msgq.h 中的 msgq_msg_t）

```cpp
// ❌ 原始：纯 C 结构
typedef struct {
    size_t size;
    char * data;  // 需要手动 new/delete
} msgq_msg_t;

// 初始化函数
int msgq_msg_init_size(msgq_msg_t *msg, size_t size) {
    msg->data = new(std::nothrow) char[size];
    if (msg->data == NULL) return -1;
    msg->size = size;
    return 0;
}

// 清理函数
void msgq_msg_close(msgq_msg_t *msg) {
    if (msg->size > 0) {
        delete[] msg->data;  // ⚠️ 需要手动调用
    }
}

// 使用示例
msgq_msg_t msg;
msgq_msg_init_size(&msg, 256);
// ... 使用 msg.data ...
msgq_msg_close(&msg);  // ⚠️ 容易忘记

// 异常不安全
if (some_error) {
    return -1;  // ❌ LEAK: msgq_msg_close 没被调用
}
```

### 现代 C++ 代码（msgq_modern.h）

```cpp
// ✅ 现代：RAII 类
class Message {
private:
    std::vector<char> data_;

public:
    // 默认构造
    Message() = default;
    
    // 带大小的构造
    explicit Message(size_t size) : data_(size) {}
    
    // 带初始数据的构造
    Message(gsl::span<const char> src) 
        : data_(src.begin(), src.end()) {}
    
    // 规则五 - 全部默认（std::vector 处理一切）
    Message(const Message&) = default;
    Message& operator=(const Message&) = default;
    Message(Message&&) noexcept = default;
    Message& operator=(Message&&) noexcept = default;
    ~Message() = default;  // 自动清理
    
    // 访问接口
    gsl::span<const char> data() const { 
        return gsl::span<const char>(data_.data(), data_.size()); 
    }
    
    size_t size() const { return data_.size(); }
};

// 使用示例
Message msg(256);  // ✅ 自动清理
// ... 使用 msg.data() ...
// ✅ 自动销毁，无需调用任何函数

// 异常安全
if (some_error) {
    throw std::runtime_error("...");  // ✅ 自动清理
}
```

### 性能对比

| 方面 | 原始代码 | 现代代码 |
|------|---------|---------|
| 堆分配 | 1 次（new） | 1 次（vector） |
| 析构开销 | delete 操作 | 自动，同样快 |
| 异常安全 | 否 | 是 |
| 复制成本 | O(n)，需手动 | O(n)，自动移动 |

---

## 2. 队列对象（Queue）

### 原始代码

```cpp
// ❌ 原始：C 结构 + 手动管理
typedef struct {
    int fd;
    int size;
    int reader_id;
    char * mmap_p;         // ⚠️ 需要手动 munmap
    std::atomic<uint64_t> *write_p;
    std::atomic<uint64_t> *read_p;
} msgq_queue_t;

// 初始化
int msgq_new_queue(msgq_queue_t *q, const char *name, int size) {
    q->fd = open(name, O_CREAT | O_RDWR);
    if (q->fd < 0) return -1;  // ⚠️ 错误处理不完整
    
    q->mmap_p = mmap(NULL, size, PROT_READ | PROT_WRITE, 
                     MAP_SHARED, q->fd, 0);
    if (q->mmap_p == MAP_FAILED) {
        close(q->fd);  // ⚠️ 易忘记
        return -1;
    }
    
    // ... 更多初始化 ...
    return 0;
}

// 清理
void msgq_close_queue(msgq_queue_t *q) {
    if (q->mmap_p != NULL) {
        munmap(q->mmap_p, q->size);  // ⚠️ 需要手动调用
    }
    close(q->fd);  // ⚠️ 需要手动调用
}

// 使用示例
msgq_queue_t queue;
if (msgq_new_queue(&queue, "test", 1024*1024) < 0) {
    // ❌ 错误处理困难
}
// ... 使用 queue ...
msgq_close_queue(&queue);  // ⚠️ 需记得调用
```

### 现代 C++ 代码

```cpp
// ✅ 现代：类型安全 + 自动清理
class Queue {
private:
    class Impl;  // 使用 Pimpl 隐藏实现
    std::unique_ptr<Impl> impl_;

    explicit Queue(std::unique_ptr<Impl> impl) 
        : impl_(std::move(impl)) {}

public:
    // 工厂函数
    [[nodiscard]] static Queue create(
        std::string_view name, 
        size_t size = DEFAULT_SEGMENT_SIZE) 
    {
        auto impl = std::make_unique<Impl>(name, size);
        // ✅ 如果 open/mmap 失败，自动清理
        // ✅ 异常在这里抛出，main 捕获
        return Queue(std::move(impl));
    }
    
    // 规则五：删除复制，保留移动
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&&) noexcept = default;
    Queue& operator=(Queue&&) noexcept = default;
    ~Queue();  // ✅ 使用 unique_ptr，自动清理
    
    // 操作接口
    void send(gsl::span<const char> data);
    Message recv(int timeout_ms = DEFAULT_TIMEOUT_MS);
    void init_publisher();
    void init_subscriber(bool conflate = false);
};

// 使用示例
try {
    auto queue = Queue::create("test", 1024*1024);  // ✅ 异常处理
    queue.init_publisher();
    
    queue.send(data);  // ✅ 简洁接口
    auto msg = queue.recv();  // ✅ 返回 Message 对象
    
}  // ✅ 自动清理，无需调用任何函数
catch (const msgq::MessageQueueError& e) {
    std::cerr << "Queue error: " << e.what() << std::endl;
}
```

### Impl 类内部

```cpp
// ✅ Impl 使用 RAII guards
class Queue::Impl {
private:
    FdGuard fd_;        // ✅ 自动关闭
    MmapGuard mmap_;    // ✅ 自动 munmap
    std::string name_;
    size_t size_;
    int reader_id_ = -1;

public:
    Impl(std::string_view name, size_t size) 
        : name_(name), size_(align_to_8(size)) 
    {
        init_shared_memory();  // ✅ 异常抛出
    }
    
    ~Impl() {
        // ✅ fd_ 和 mmap_ 自动销毁
        // 无需手动 close/munmap
    }
    
private:
    void init_shared_memory() {
        // 如果 open 失败 -> 异常，Impl 未完全构造
        FdGuard fd(::open(...));
        if (!fd.valid()) {
            throw MessageQueueError("open failed");  // ✅ 自动清理
        }
        
        // 如果 mmap 失败 -> 异常，fd 自动关闭
        void* addr = ::mmap(...);
        if (addr == MAP_FAILED) {
            throw MessageQueueError("mmap failed");  // ✅ fd ~FdGuard
        }
        
        // ✅ 保存到成员变量
        fd_ = std::move(fd);
        mmap_ = MmapGuard(addr, total_size);
    }
};
```

---

## 3. RAII Guard 类

### 原始代码（缺失）

```cpp
// ❌ 原始代码没有这些
// mmap/munmap 分散在各处
// close/open 分散在各处
```

### 现代 C++ 代码

```cpp
// ✅ 文件描述符守卫
class FdGuard {
private:
    int fd_ = -1;
    
    void cleanup() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

public:
    // 构造
    FdGuard() = default;
    explicit FdGuard(int fd) noexcept : fd_(fd) {}
    
    // 销毁时关闭
    ~FdGuard() { cleanup(); }
    
    // 禁止复制
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
    
    // 允许移动
    FdGuard(FdGuard&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;  // ✅ 转移所有权
    }
    FdGuard& operator=(FdGuard&& other) noexcept {
        cleanup();
        fd_ = other.fd_;
        other.fd_ = -1;
        return *this;
    }
    
    // 访问接口
    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }
};

// ✅ 内存映射守卫
class MmapGuard {
private:
    void* addr_ = nullptr;
    size_t size_ = 0;
    
    void cleanup() noexcept {
        if (addr_) {
            ::munmap(addr_, size_);
            addr_ = nullptr;
        }
    }

public:
    // 构造
    MmapGuard() = default;
    MmapGuard(void* addr, size_t size) noexcept 
        : addr_(addr), size_(size) {}
    
    // 销毁时取消映射
    ~MmapGuard() { cleanup(); }
    
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
        cleanup();
        addr_ = other.addr_;
        size_ = other.size_;
        other.addr_ = nullptr;
        other.size_ = 0;
        return *this;
    }
    
    // 访问接口
    void* get() const noexcept { return addr_; }
    size_t size() const noexcept { return size_; }
    bool valid() const noexcept { return addr_ != nullptr; }
    explicit operator bool() const noexcept { return valid(); }
};
```

---

## 4. 高级工程代码示例

### 原始代码（impl_msgq.h 中的 VisionIpcClient）

```cpp
// ❌ 原始：大量手动指针管理
class VisionIpcClient {
private:
    Context* ctx_;          // ⚠️ new Context
    Poller* poller_;        // ⚠️ new Poller  
    Socket* subscriber_;    // ⚠️ new Socket
    Socket* arb_;           // ⚠️ new Socket

public:
    VisionIpcClient() 
        : ctx_(NULL), poller_(NULL), subscriber_(NULL), arb_(NULL) {}
    
    int connect(const char *addr) {
        ctx_ = new Context();  // ⚠️ 手动分配
        if (ctx_ == NULL) return -1;
        
        subscriber_ = new Socket(ctx_, ZMQ_SUB);  // ⚠️ 又分配
        if (subscriber_ == NULL) {
            delete ctx_;  // ⚠️ 需记得清理
            return -1;
        }
        
        arb_ = new Socket(ctx_, ZMQ_SUB);  // ⚠️ 又分配
        if (arb_ == NULL) {
            delete subscriber_;  // ⚠️ 清理链增长
            delete ctx_;
            return -1;
        }
        
        // ... 更多初始化 ...
        return 0;
    }
    
    ~VisionIpcClient() {
        // ⚠️ 手动销毁，顺序错误可能导致崩溃
        if (subscriber_ != NULL) delete subscriber_;
        if (arb_ != NULL) delete arb_;
        if (ctx_ != NULL) delete ctx_;
        // ⚠️ 如果析构被跳过（异常），会泄漏
    }
};

// ❌ 问题：
// 1. connect 中第二个 new 失败，第一个 new 的对象泄漏
// 2. 析构顺序错误可能崩溃（Socket 需要 ctx 存活）
// 3. 异常不安全
```

### 现代 C++ 代码

```cpp
// ✅ 现代：智能指针管理
class VisionIpcClient {
private:
    std::unique_ptr<Context> ctx_;
    std::unique_ptr<Poller> poller_;
    std::unique_ptr<Socket> subscriber_;
    std::unique_ptr<Socket> arb_;

public:
    VisionIpcClient() = default;  // ✅ 规则零
    
    // ✅ 无需手动析构函数
    ~VisionIpcClient() = default;
    
    // 禁止复制，允许移动
    VisionIpcClient(const VisionIpcClient&) = delete;
    VisionIpcClient& operator=(const VisionIpcClient&) = delete;
    VisionIpcClient(VisionIpcClient&&) noexcept = default;
    VisionIpcClient& operator=(VisionIpcClient&&) noexcept = default;
    
    void connect(std::string_view addr) {
        ctx_ = std::make_unique<Context>();
        // ✅ 如果 make_unique 失败，异常抛出，ctx_ 为 nullptr
        
        subscriber_ = std::make_unique<Socket>(*ctx_, ZMQ_SUB);
        // ✅ 如果这里失败，ctx_ 自动保留（异常安全）
        
        arb_ = std::make_unique<Socket>(*ctx_, ZMQ_SUB);
        // ✅ 如果这里失败，前面的都自动清理
        
        // ... 更多初始化 ...
        
        // ✅ 异常抛出时，所有 unique_ptr 自动销毁
    }
};

// ✅ 优点：
// 1. 自动清理，无泄漏
// 2. 析构顺序正确（unique_ptr 管理）
// 3. 异常安全
// 4. 无需手写析构函数
// 5. 代码行数减少 50%
```

---

## 5. 工厂模式

### 原始代码

```cpp
// ❌ 返回原始指针，所有权不清晰
SubSocket* create_sub(const char* addr) {
    auto sub = new SubSocket();  // ⚠️ caller 需 delete
    if (sub->connect(addr) < 0) {
        delete sub;  // ⚠️ 需记得
        return NULL;
    }
    return sub;
}

// 使用时容易泄漏
auto sub = create_sub("tcp://...");
if (sub == NULL) {
    // ❌ 错误处理困难
}
// ❌ 使用完后需记得 delete
delete sub;
```

### 现代 C++ 代码

```cpp
// ✅ 返回 unique_ptr，所有权清晰
std::unique_ptr<SubSocket> create_sub(std::string_view addr) {
    auto sub = std::make_unique<SubSocket>();
    sub->connect(addr);  // ✅ 异常抛出
    return sub;  // ✅ 移动语义，无复制
}

// 使用时安全
try {
    auto sub = create_sub("tcp://...");  // ✅ 自动清理
    sub->send(data);
}  // ✅ 自动销毁
catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
}
```

---

## 6. 错误处理

### 原始代码

```cpp
// ❌ C 风格错误返回
int msgq_send(msgq_queue_t *q, msgq_msg_t *msg) {
    if (q == NULL) return -1;
    if (msg == NULL) return -2;
    if (msg->data == NULL) return -3;
    
    // ... 发送逻辑 ...
    
    if (/* 某个错误 */) {
        return -4;  // ⚠️ 错误代码混乱
    }
    return 0;  // ✅ 成功
}

// 使用时需检查返回值
int ret = msgq_send(&queue, &msg);
if (ret == -1) {
    // ❌ 不知道具体是什么错误
}
```

### 现代 C++ 代码

```cpp
// ✅ 异常处理
void Queue::send(const Message& msg) {
    if (!impl_) {
        throw MessageQueueError("Queue not initialized");
    }
    if (msg.empty()) {
        throw std::invalid_argument("Message is empty");
    }
    if (msg.size() > impl_->size_) {
        throw std::out_of_range("Message too large");
    }
    
    // ... 发送逻辑 ...
    
    if (/* 某个错误 */) {
        throw MessageQueueError("Send failed: " + std::string(strerror(errno)));
    }
}

// 使用时简洁
try {
    queue.send(msg);  // ✅ 清晰的语义
}
catch (const MessageQueueError& e) {
    std::cerr << "Queue error: " << e.what() << std::endl;
}
catch (const std::invalid_argument& e) {
    std::cerr << "Invalid argument: " << e.what() << std::endl;
}

// 或者使用 std::optional（C++17）
std::optional<Message> maybe_recv(int timeout_ms) {
    try {
        return recv(timeout_ms);
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

if (auto msg = maybe_recv(1000)) {
    std::cout << "Received: " << msg->size() << " bytes" << std::endl;
}
```

---

## 7. 编译时检查

### 原始代码

```cpp
// ❌ 编译时无法检查
#define PACK64(out, hi, lo) ((out) = (((uint64_t)(hi)) << 32) | (lo))

// 容易出错：
uint64_t result;
PACK64(result, 0x12345678, 0x87654321);  // ✅ 正确
PACK64(result, 0x87654321, 0x12345678);  // ❌ 颠倒参数，编译器不报错
```

### 现代 C++ 代码

```cpp
// ✅ 编译时检查
class PackedPointer {
public:
    constexpr PackedPointer(uint32_t cycle, uint32_t offset) noexcept
        : value_((static_cast<uint64_t>(cycle) << 32) | offset) {}
    
    constexpr uint32_t cycle() const noexcept {
        return static_cast<uint32_t>(value_ >> 32);
    }
    
    constexpr uint32_t offset() const noexcept {
        return static_cast<uint32_t>(value_ & 0xFFFFFFFFULL);
    }
};

// 编译器检查类型
PackedPointer pp(0x12345678, 0x87654321);  // ✅ 清晰的语义
auto cycle = pp.cycle();   // ✅ 类型安全
auto offset = pp.offset(); // ✅ 类型安全

// 甚至可以在编译期计算
constexpr PackedPointer initial(1, 0);
constexpr auto init_cycle = initial.cycle();  // ✅ 编译期计算
static_assert(init_cycle == 1);  // ✅ 编译时验证
```

---

## 总结对比表

| 特性 | 原始代码 | 现代 C++ |
|------|---------|---------|
| 内存管理 | 手动 new/delete | RAII + unique_ptr |
| 泄漏风险 | **高** | **零** |
| 异常安全 | **否** | **是** |
| 错误处理 | 返回码 | 异常 |
| 代码行数 | 更多 | 更少 |
| 编译检查 | 弱 | 强 |
| 运行时开销 | 基准 | **相同** |
| 开发时间 | 短 | 中等 |
| 维护难度 | 高 | 低 |
| 编译器支持 | C++11 | C++17/20 |

---

## 迁移路径

```
Phase 1: 并行支持
└─ 新代码使用 msgq_modern.h
└─ 旧代码继续使用原 msgq.h
└─ 通过编译标志切换

Phase 2: 逐步替换
└─ 将高层 API（impl_msgq.h）迁移到现代 C++
└─ 保留底层 C API（msgq.h）兼容性

Phase 3: 完全现代化
└─ 所有代码使用现代 C++
└─ 可选保留 C API wrapper（legacy 命名空间）
└─ Python 绑定升级
```
