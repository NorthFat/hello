# 📊 std::span 兼容性指南

## 概述

`msgq_modern.h` 现已完全兼容 **C++20 `std::span`**，同时保持对 **C++17 GSL::span** 和 **自定义 span** 实现的向后兼容性。

## 支持策略

### 分层支持

```cpp
C++20              ➜ std::span (标准库)
C++17 + GSL        ➜ gsl::span (GSL库)
C++17 (无依赖)     ➜ msgq::span (自定义实现)
```

### 自动选择

编译器根据 C++ 标准版本和可用的库自动选择最合适的 span 实现：

```cpp
// 编译时自动选择：
namespace msgq {
  #if __cplusplus >= 202002L
    // C++20: 直接使用 std::span
    template<typename T>
    using span = std::span<T>;
  
  #elif MSGQ_HAS_GSL
    // C++17 + GSL: 使用 gsl::span
    using gsl::span;
  
  #else
    // C++17 (纯): 使用自定义实现
    template<typename T>
    class span { /* ... */ };
  #endif
}
```

## 使用示例

### ✅ 统一接口（所有 C++ 版本）

```cpp
#include "msgq_modern.h"
using msgq::span;

// 创建 span
std::vector<char> data = {1, 2, 3, 4, 5};
span<char> s = msgq::make_span(data);

// 基本操作
char* ptr = s.data();
size_t size = s.size();
bool empty = s.empty();

// 迭代
for (char c : s) {
    std::cout << (int)c << " ";
}

// 子 span
span<char> first_3 = s.first(3);        // [1, 2, 3]
span<char> last_2 = s.last(2);          // [4, 5]
span<char> sub = s.subspan(1, 2);       // [2, 3]
```

### 🎯 C++20 特定特性

#### 1. 直接使用 `std::span`

```cpp
#include "msgq_modern.h"
#include <span>

// 直接兼容 std::span
msgq::Queue queue = msgq::Queue::create("my_queue");

// 使用 std::span 发送
std::vector<char> data = {1, 2, 3};
std::span<char> payload(data);
queue.send(payload);  // ✅ 自动转换

// 使用 std::span 类型
std::array<char, 5> buffer = {5, 4, 3, 2, 1};
std::span<const char> const_payload(buffer);
queue.send(const_payload);  // ✅ 自动转换
```

#### 2. 消息中的 `std::span` 访问

```cpp
msgq::Message msg = queue.recv();

// 作为 std::span 访问
std::span<const char> view = msg.as_span();
for (size_t i = 0; i < view.size(); ++i) {
    std::cout << (int)view[i] << " ";
}
```

#### 3. 泛型类型支持（仅 C++20）

```cpp
// 发送结构化数据
struct Point {
    float x, y;
};

std::vector<Point> points = {{1.0f, 2.0f}, {3.0f, 4.0f}};
std::span<Point> point_span(points);
queue.send(point_span);  // ✅ 自动字节转换

// 接收为具体类型
msgq::Message msg = queue.recv();
std::span<const Point> received = msg.as_span<Point>();
for (const auto& p : received) {
    std::cout << "(" << p.x << ", " << p.y << ") ";
}
```

### 🔄 C++17 兼容代码

#### 使用自定义 span（无依赖）

```cpp
#include "msgq_modern.h"

// msgq::span 在编译时自动选择最佳实现
msgq::Queue queue = msgq::Queue::create("queue_17");

std::vector<char> data = {1, 2, 3};
msgq::span<const char> s = msgq::make_span(data);

// 或直接构造
msgq::span<char> direct(data.data(), data.size());

// 所有 span 操作都支持
std::cout << "Size: " << s.size() << std::endl;
queue.send(s);
```

#### 使用 GSL（带 GSL 库）

```cpp
#include "msgq_modern.h"
#include <gsl/gsl>

msgq::Queue queue = msgq::Queue::create("gsl_queue");

std::vector<char> data = {1, 2, 3};

// msgq::span 实际使用 gsl::span
msgq::span<char> s = msgq::make_span(data);

// 也可直接用 gsl::span
gsl::span<char> gsl_span(data);
queue.send(gsl_span);  // ✅ 完全兼容
```

## API 参考

### `msgq::make_span()` 助手函数

```cpp
// 从指针和大小创建
template<typename T>
constexpr span<T> make_span(T* data, size_t size) noexcept;

// 从容器创建（推导类型）
template<typename Container>
constexpr auto make_span(Container& c) noexcept;
template<typename Container>
constexpr auto make_span(const Container& c) noexcept;
```

### `msgq::span<T>` 接口（统一 API）

#### 构造

```cpp
// 默认构造 - 空 span
span<T> s;

// 从指针和大小
span<T> s(data, size);

// 从容器
std::vector<T> vec = {...};
span<T> s(vec);          // 非常量容器
span<const T> s2(vec);   // 常量容器
```

#### 元素访问

```cpp
span<T> s = ...;

// 数据和大小
T* ptr = s.data();
size_t sz = s.size();
size_t bytes = s.size_bytes();  // C++20 only
bool empty = s.empty();

// 索引访问
T& elem = s[0];
T& first = s.front();
T& last = s.back();
```

#### 迭代

```cpp
span<T> s = ...;

// 迭代器
for (T* it = s.begin(); it != s.end(); ++it) { }
for (const auto* it = s.cbegin(); it != s.cend(); ++it) { }

// 范围 for（仅 C++20 std::span）
#if __cplusplus >= 202002L
for (T& elem : s) { }
#endif
```

#### 子 span

```cpp
span<T> s = ...;

// 获取前 n 个元素
span<T> first_3 = s.first(3);

// 获取后 n 个元素
span<T> last_2 = s.last(2);

// 获取从 offset 开始的 count 个元素（仅 C++20 std::span）
#if __cplusplus >= 202002L
span<T> sub = s.subspan(1, 3);
#endif
```

### `msgq::Message` 的 span 访问

#### C++17 兼容（通用）

```cpp
msgq::Message msg = ...;

// GSL/自定义 span
msgq::span<const char> s = msg.data();
msgq::span<char> mutable_s = msg.data();
```

#### C++20 特定

```cpp
msgq::Message msg = ...;

// 作为 std::span
std::span<const char> s = msg.as_span();
std::span<char> mutable_s = msg.as_span();

// 作为类型化 span
struct MyData { int x; float y; };
std::span<const MyData> typed = msg.as_span<MyData>();
```

### 消息构造

#### C++17 兼容

```cpp
// 从 GSL/自定义 span
msgq::span<const char> s = ...;
msgq::Message msg(s);
```

#### C++20 特定

```cpp
// 从 std::span
std::span<const char> s = ...;
msgq::Message msg(s);

// 从类型化数据（自动字节转换）
std::vector<int> data = {1, 2, 3};
std::span<int> typed(data);
msgq::Message msg(typed);  // ✅ 自动转换为字节
```

## 完整代码示例

### 示例 1: 跨越 C++ 版本的兼容代码

```cpp
#include "msgq_modern.h"

void send_data(const std::vector<char>& data) {
    msgq::Queue queue = msgq::Queue::create("compat_queue");
    
    // 在所有 C++ 版本中都有效
    auto span = msgq::make_span(data);
    queue.send(span);
}
```

### 示例 2: C++20 的现代用法

```cpp
#if __cplusplus >= 202002L

#include "msgq_modern.h"
#include <span>
#include <array>

void modern_send(const std::array<char, 5>& data) {
    msgq::Queue queue = msgq::Queue::create("modern_queue");
    
    // 直接使用 std::span
    std::span<const char> view(data);
    queue.send(view);
    
    // 或推导类型
    queue.send(msgq::make_span(data));
}

#endif
```

### 示例 3: 结构化数据传输（C++20）

```cpp
#if __cplusplus >= 202002L

#include "msgq_modern.h"
#include <span>

struct Message {
    uint32_t id;
    float value;
    char name[32];
};

void send_structured() {
    msgq::Queue queue = msgq::Queue::create("struct_queue");
    
    Message msg{42, 3.14f, "test"};
    std::span<const Message> payload(&msg, 1);
    
    // 自动转换为字节
    queue.send(payload);
}

void receive_structured() {
    msgq::Queue queue = msgq::Queue::create("struct_queue");
    
    auto msg_buf = queue.recv();
    auto messages = msg_buf.as_span<Message>();
    
    for (const auto& m : messages) {
        std::cout << "ID: " << m.id << std::endl;
    }
}

#endif
```

## 编译和构建

### 编译选项

```bash
# C++20（推荐，获得所有特性）
g++ -std=c++20 -O2 your_code.cc

# C++17 + GSL
g++ -std=c++17 -O2 your_code.cc -lgsl

# C++17（纯粹，无外部依赖）
g++ -std=c++17 -O2 your_code.cc
```

### CMake 配置

```cmake
project(msgq_example)

set(CMAKE_CXX_STANDARD 20)  # 或 17
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(example example.cc)
target_link_libraries(example PRIVATE msgq)

# 如果需要 GSL（可选）
# find_package(gsl REQUIRED)
# target_link_libraries(example PRIVATE gsl)
```

## 性能考虑

### 零开销抽象

- `span` 在编译时完全内联
- 无运行时开销（指针 + 大小）
- C++20 `std::span` 和自定义实现性能相同

### 推荐做法

```cpp
// ✅ 好：传递 span 避免复制
void process(msgq::span<const char> data) {
    // 处理 data
}

// ❌ 避免：复制数据
void process(const std::vector<char>& data) {
    // 数据被复制
}

// ❌ 避免：原始指针
void process(const char* data, size_t size) {
    // 易出错，不安全
}
```

## 常见问题

### Q: 我应该使用哪个 span 版本？

**A:** 优先级顺序：
1. `std::span`（如果 C++20 可用）
2. `msgq::span`（推荐，自动选择最佳）
3. `gsl::span`（需要 GSL 库）
4. 原始指针（避免）

### Q: 如何在 C++17 中使用类型化 span？

**A:** 使用 `data()` 和大小计算，或升级到 C++20：

```cpp
// C++17 方式
std::vector<int> ints = {1, 2, 3};
msgq::span<int> s(ints.data(), ints.size());

// C++20 方式
std::span<int> s(ints);
```

### Q: span 是否执行边界检查？

**A:** 自定义实现不执行运行时边界检查（性能优先）。
C++20 `std::span` 也不进行运行时检查，除非使用调试模式。

使用 `assert` 或其他验证框架进行调试：

```cpp
// 手动验证
assert(index < s.size());
s[index];
```

### Q: 能否混合使用不同的 span 类型？

**A:** 可以，都能自动转换：

```cpp
msgq::span<char> s1 = ...;
std::span<char> s2 = ...;  // C++20
gsl::span<char> s3 = ...;  // 如果使用 GSL

// 在内部都转换为 msgq::span
queue.send(s1);
queue.send(s2);  // 自动转换
queue.send(s3);  // 自动转换
```

## 迁移指南

### 从原始指针迁移

```cpp
// 旧代码
void send_old(const char* data, size_t size) {
    queue.send(data, size);  // 不支持
}

// 新代码
void send_new(msgq::span<const char> data) {
    queue.send(data);  // ✅
}

// 调用
std::vector<char> v = {1, 2, 3};
send_new(msgq::make_span(v));  // ✅
```

### 从 `std::vector` 迁移

```cpp
// 旧代码
void send_old(const std::vector<char>& data) {
    queue.send(data.data(), data.size());
}

// 新代码
void send_new(msgq::span<const char> data) {
    queue.send(data);
}

// 调用方保持不变
std::vector<char> v = {1, 2, 3};
send_new(v);  // ✅ 自动转换
```

---

## 总结

| 特性 | C++17 自定义 | C++17 + GSL | C++20 std |
|------|------------|-----------|---------|
| 基本操作 | ✅ | ✅ | ✅ |
| 类型安全 | ✅ | ✅ | ✅ |
| 零开销 | ✅ | ✅ | ✅ |
| 范围 for | ❌ | ❌ | ✅ |
| 子 span | ⚠️ | ⚠️ | ✅ |
| 字节转换 | ✅ | ✅ | ✅ |
| 外部依赖 | ❌ | ✅ | ❌ |
| 推荐使用 | 🥉 | 🥈 | 🥇 |

**推荐：** 使用 `msgq::span` 或 `msgq::make_span()`，自动适配最佳实现！

---

**文档版本:** 1.0  
**最后更新:** 2024-12-16  
**C++ 标准:** C++17, C++20  
**依赖:** 可选（GSL）
