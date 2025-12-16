# msgq.h 现代 C++ 重构指南

## 概述

本文档说明如何将原有的 C 风格 `msgq.h` 转换为符合现代 C++ 标准（C++17/20）的版本，同时保持性能和兼容性。

## 核心改进

### 1. RAII 资源管理 (Rule of Zero)

**问题：原代码**
```cpp
// ❌ 原始代码 - 手动管理
char* data = new char[size];
// ... 使用 ...
delete[] data;  // 容易泄漏

void* addr = mmap(...);
// ... 使用 ...
munmap(addr, size);  // 容易忘记
```

**改进：现代 C++**
```cpp
// ✅ RAII 包装
class MmapGuard {
private:
    void* addr_ = nullptr;
    size_t size_ = 0;
public:
    MmapGuard(void* addr, size_t size) : addr_(addr), size_(size) {}
    ~MmapGuard() {
        if (addr_) ::munmap(addr_, size_);
    }
    // 支持移动语义
    MmapGuard(MmapGuard&& other) noexcept 
        : addr_(other.addr_), size_(other.size_) {
        other.addr_ = nullptr;
    }
    // 禁止复制
    MmapGuard(const MmapGuard&) = delete;
};
```

### 2. 智能指针替代原始指针

**问题：原代码**
```cpp
// ❌ 容易泄漏，所有权不清晰
MSGQSubSocket* sub = new MSGQSubSocket();
// ... 如果异常发生，无法清理
if (error) return;  // LEAK!
```

**改进：现代 C++**
```cpp
// ✅ 清晰的所有权，自动清理
auto sub = std::make_unique<Queue>();
// 不需要手动 delete，异常安全
if (error) throw std::runtime_error("...");  // 自动清理 sub
```

### 3. STL 容器替代手动数组

**问题：原代码**
```cpp
// ❌ 手动管理
char* data = new char[size];
strcpy(data, source);
// ... 使用 ...
delete[] data;
```

**改进：现代 C++**
```cpp
// ✅ STL 容器，自动管理
class Message {
private:
    std::vector<char> data_;
public:
    Message() = default;
    explicit Message(size_t size) : data_(size) {}
    Message(const Message&) = default;  // 自动实现
    ~Message() = default;  // 自动清理
};
```

### 4. 类型安全和 constexpr

**问题：原代码**
```cpp
// ❌ C 风格宏，缺乏类型安全
#define PACK64(output, higher, lower) \
    ((output) = (((uint64_t)(higher)) << 32) | (lower))

#define UNPACK64(higher, lower, input) \
    {(higher) = ((input) >> 32); (lower) = (input) & 0xFFFFFFFFULL;}
```

**改进：现代 C++**
```cpp
// ✅ 类型安全，编译期计算
class PackedPointer {
    uint64_t value_;
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

// 编译期使用
constexpr PackedPointer ptr(1, 0);
constexpr auto c = ptr.cycle();  // 编译期计算
```

### 5. 异常安全和错误处理

**问题：原代码**
```cpp
// ❌ C 风格错误返回
int msgq_new_queue(msgq_queue_t* q, const char* name, int size) {
    q->fd = open(...);
    if (q->fd < 0) return -1;  // 清理不完整
    
    q->mmap_p = mmap(...);
    if (q->mmap_p == MAP_FAILED) {
        close(q->fd);  // 易忘记
        return -1;
    }
    return 0;
}
```

**改进：现代 C++**
```cpp
// ✅ 异常安全
Queue Queue::create(std::string_view name, size_t size) {
    auto impl = std::make_unique<Impl>(name, size);
    // 如果 open 失败，异常抛出
    // 如果 mmap 失败，fd 自动关闭（通过 ~Impl）
    return Queue(std::move(impl));
}
```

### 6. 现代概念（C++20）

**新增功能**
```cpp
// ✅ 类型约束
template<typename T>
concept Sendable = requires(const T& t) {
    { t.data() } -> std::convertible_to<gsl::span<const char>>;
};

// 现在编译器可以验证类型要求
void send(const Sendable auto& data) {
    // 只接受支持 .data() 的类型
}
```

### 7. 移动语义和完美转发

**改进点**
```cpp
// ✅ 支持移动语义，避免不必要复制
Message msg1 = Message(buffer);  // 无复制
Message msg2 = std::move(msg1);   // 无复制

// ✅ 完美转发工厂函数
template<typename... Args>
static Queue create_from(Args&&... args) {
    auto impl = std::make_unique<Impl>(std::forward<Args>(args)...);
    return Queue(std::move(impl));
}
```

## 设计决策

### 1. 为什么保留底层 C API？

**原因：**
- Python Cython 绑定依赖 C API（ipc_pyx.pyx）
- 现有测试代码使用 C 接口（msgq_tests.cc）
- 兼容性要求

**方案：**
```cpp
namespace legacy {
    // 原始 C 风格函数（通过 C++ 包装实现）
    // 标记为 [[deprecated]]
    [[deprecated("use Queue::create instead")]]
    msgq_queue_t* msgq_new_queue(const char* name, int size);
}
```

### 2. 为什么使用 gsl::span？

**优点：**
- 无所有权的数据视图
- 避免多余的参数（指针 + 大小）
- 边界检查可选

**用法：**
```cpp
// 而不是：
void send(char* data, size_t size);

// 使用：
void send(gsl::span<const char> data);  // 更清晰
```

### 3. 为什么使用 Pimpl 模式？

**对象布局：**
```
Queue (用户看到)
  |
  +-- unique_ptr<Impl>
      |
      +-- Impl (内部实现)
          |
          +-- FdGuard fd_
          +-- MmapGuard mmap_
          +-- Header* header_
          +-- char* data_start_
          +-- 其他字段...
```

**好处：**
- 隐藏实现细节
- 稳定的 ABI（二进制兼容）
- 易于后期优化

## 迁移指南

### 步骤 1：编译新代码

```bash
# 需要 C++17 支持
g++ -std=c++17 -I/path/to/GSL/include msgq_modern.cc -o test
```

### 步骤 2：更新现有代码

**旧代码：**
```cpp
msgq_queue_t sub_queue;
msgq_new_queue(&sub_queue, "sub", 1024 * 1024);
// ... 使用 ...
msgq_close_queue(&sub_queue);
```

**新代码：**
```cpp
auto sub_queue = msgq::Queue::create("sub", 1024 * 1024);
sub_queue.init_subscriber();
// ... 使用 ...
// 自动清理，无需调用任何 close 函数
```

### 步骤 3：逐步迁移

**过渡期：**
```cpp
// 同时支持两种 API
#ifdef USE_MODERN_API
    auto queue = msgq::Queue::create(...);
#else
    msgq_queue_t queue;
    msgq_new_queue(&queue, ...);
#endif
```

## 性能考虑

### 1. 零开销抽象

- `PackedPointer` 全是 constexpr，编译期消除
- `Message` 通过 RVO 避免复制
- `MmapGuard` 只在销毁时执行操作
- 内联虚函数调用

### 2. 共享内存布局不变

原始数据结构保持不变：
```cpp
// 共享内存布局（不变）
struct SharedHeader {
    std::atomic<uint64_t> write_index;          // 8 字节
    std::atomic<uint64_t> read_index[NUM_READERS];  // 8*15 字节
    uint32_t num_readers;                       // 4 字节
    // ... 其他字段
};
```

### 3. 内存使用相同

- 不增加堆分配（使用栈上的 unique_ptr）
- 不增加同步开销（相同的原子操作）
- 无额外的虚函数调用

## 编译检查

### 1. 编译时约束

```cpp
// 编译器验证
static_assert(std::is_move_constructible_v<Queue>);
static_assert(std::is_move_assignable_v<Queue>);
static_assert(!std::is_copy_constructible_v<Queue>);
static_assert(std::is_nothrow_destructible_v<Queue>);
```

### 2. 概念验证（C++20）

```cpp
// 在编译期验证
static_assert(msgq::Sendable<Message>);
```

## 测试策略

### 单元测试示例

```cpp
TEST(QueueTest, RawDataSend) {
    auto queue = msgq::Queue::create("test", 1024 * 1024);
    queue.init_publisher();
    
    std::string data = "Hello, World!";
    queue.send(gsl::span(data));
}

TEST(QueueTest, MessageSend) {
    auto queue = msgq::Queue::create("test", 1024 * 1024);
    queue.init_publisher();
    
    msgq::Message msg(10);
    std::fill(msg.data().begin(), msg.data().end(), 'A');
    queue.send(msg);
}

TEST(QueueTest, AutoCleanup) {
    // 对象超出作用域自动清理
    {
        auto queue = msgq::Queue::create("temp", 1024);
    }  // 自动 munmap, close
    // 验证资源被释放
}
```

## 与原 C API 的关系

```
原始 msgq.h (C)
     ↓
msgq_modern.h (C++)
     ├─ 提供现代化接口
     ├─ 保持 zero-overhead
     └─ 可选保留 C 兼容性层（legacy::）

Python 绑定 (Cython)
     ├─ 可直接使用 C API（不变）
     └─ 将来可升级使用 C++ 接口
```

## 总结

| 方面 | 原始代码 | 现代 C++ |
|------|---------|---------|
| 内存管理 | 手动 new/delete | std::vector + unique_ptr |
| 资源泄漏风险 | 高 | 零（RAII） |
| 异常安全 | 否 | 是 |
| 类型安全 | 低（宏） | 高（类型系统） |
| 编译优化 | 有限 | 更好（constexpr） |
| 性能 | ≈同 | ≈同（零开销） |
| 代码复杂性 | 低 | 中等（但更安全） |
| 学习曲线 | 平缓 | 陡峭（但更现代） |

## 下一步

1. **集成 GSL 库**：添加 `gsl::span`, `gsl::not_null` 支持
2. **性能测试**：验证零开销抽象
3. **迁移 impl_msgq.h**：使用 unique_ptr 替换原始指针
4. **迁移 VisionIpc**：应用相同模式
5. **更新 Python 绑定**：支持两种 API
6. **文档更新**：编写新 API 使用指南
