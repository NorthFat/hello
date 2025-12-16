# event_modern.h/cc 迁移指南

## 快速开始

### 1. 包含头文件

**旧版本：**
```cpp
#include "event.h"
```

**新版本（完全兼容）：**
```cpp
#include "event_modern.h"
```

### 2. 基本 API 用法

#### Event 类

**创建和销毁：**
```cpp
// 创建事件（需要有效的 eventfd）
int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
msgq::event::Event event(fd);

// ✅ 自动关闭（RAII）
// 作用域结束时，event_fd 自动关闭
```

**设置和等待：**
```cpp
// 触发事件
event.set();

// 等待事件（无超时）
try {
    event.wait();
    std::cout << "Event triggered!" << std::endl;
} catch (const std::exception& e) {
    std::cerr << "Wait failed: " << e.what() << std::endl;
}

// 等待事件（有超时）
try {
    event.wait(5);  // 等待 5 秒
} catch (const std::exception& e) {
    std::cerr << "Timeout or error: " << e.what() << std::endl;
}

// 读取计数并清除
int count = event.clear();
std::cout << "Event triggered " << count << " times" << std::endl;
```

**查询和检测：**
```cpp
// 检查事件是否有效
if (event.is_valid()) {
    std::cout << "Event ready" << std::endl;
}

// 非阻塞检查事件是否有数据
if (event.peek()) {
    event.clear();
}

// 获取底层文件描述符
int raw_fd = event.fd();
```

#### 等待多个事件

**旧版本（VLA 问题）：**
```cpp
std::vector<msgq::event::Event> events;
// ... 添加事件 ...

int idx = msgq::event::Event::wait_for_one(events, 5);
std::cout << "Event " << idx << " triggered" << std::endl;
```

**新版本（改进）：**
```cpp
std::vector<msgq::event::Event> events;
events.push_back(Event(fd1));
events.push_back(Event(fd2));
events.push_back(Event(fd3));

try {
    int triggered_idx = msgq::event::Event::wait_for_one(events, 5);
    std::cout << "Event " << triggered_idx << " was triggered" << std::endl;
} catch (const std::exception& e) {
    std::cerr << "Poll failed: " << e.what() << std::endl;
}
```

#### SocketEventHandle 类

**旧版本：**
```cpp
// 创建处理器
msgq::event::SocketEventHandle handle("socket_name");

// 获取事件
auto evt_recv = handle.recv_called();
auto evt_ready = handle.recv_ready();

evt_recv.set();
evt_ready.wait();

// ⚠️ 析构时可能出现异常不安全问题
```

**新版本（改进）：**
```cpp
// 创建处理器（相同 API）
msgq::event::SocketEventHandle handle("socket_name", "prefix", true);

// ✅ 获取事件（返回移动后的 Event）
auto evt_recv = handle.recv_called();
auto evt_ready = handle.recv_ready();

// ✅ 使用事件
evt_recv.set();
evt_ready.wait();

// ✅ 自动清理（异常安全）
// 作用域结束时：
// - close() fd_recv_called
// - close() fd_recv_ready  
// - munmap() 共享内存
// - unlink() 临时文件
// 所有操作都正确排序且无异常抛出
```

---

## 迁移清单

### [ ] 步骤 1：更新包含

```bash
# 查找所有包含旧头文件的文件
grep -r "#include.*event\.h" --include="*.cc" --include="*.h" .

# 替换为新版本
sed -i 's/#include "event\.h"/#include "event_modern.h"/g' *.cc *.h
sed -i 's/#include <event\.h>/#include <event_modern.h>/g' *.cc *.h
```

### [ ] 步骤 2：检查 API 用法

```cpp
// 需要更新的模式：

// ❌ 旧：直接访问 event_fd（private）
// int fd = event.event_fd;  

// ✅ 新：使用公开方法
int fd = event.fd();

// ❌ 旧：手动检查 nullptr
// if (mem == NULL) ...

// ✅ 新：检查 MAP_FAILED
// if (mem == MAP_FAILED) ...
```

### [ ] 步骤 3：错误处理更新

```cpp
// ❌ 旧：混合错误处理
int count = event.clear();  // 可能返回 -1
if (count < 0) { ... }

// ✅ 新：统一使用异常
try {
    int count = event.clear();
} catch (const std::exception& e) {
    // 处理错误
}
```

### [ ] 步骤 4：平台特定代码

```cpp
// ❌ 旧：编译时检查
#ifdef __APPLE__
    event.wait();  // 在 macOS 编译时被 assert 阻止
#endif

// ✅ 新：运行时检查
try {
    event.wait();  // 在 macOS 运行时抛异常
} catch (const std::runtime_error& e) {
    std::cerr << "Not supported on this platform: " << e.what() << std::endl;
}
```

### [ ] 步骤 5：编译和测试

```bash
# 编译检查
g++ -std=c++17 -Wall -Wextra -c your_file.cc

# 运行测试
./run_tests.sh

# 检查内存泄漏
valgrind --leak-check=full ./your_program

# 检查线程问题
threadsan ./your_program
```

---

## 常见问题

### Q1：如何从旧版本的 Event 迁移到新版本？

**A：** API 完全兼容，只需更新包含即可：

```cpp
// ✅ 这段代码同时适用于旧版本和新版本
msgq::event::Event event(fd);
event.set();
event.wait();
```

### Q2：如何处理 eventfd 创建错误？

**旧版本：** 
```cpp
int fd = eventfd(0, EFD_NONBLOCK);
if (fd < 0) {
    perror("eventfd");
    return -1;  // 返回错误码
}
```

**新版本：**
```cpp
try {
    msgq::event::EventfdGuard fd_guard(
        eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)
    );
    if (!fd_guard.valid()) {
        throw std::runtime_error("Failed to create eventfd: " + 
                               std::string(strerror(errno)));
    }
    msgq::event::Event event(fd_guard.get());
    // ... 使用 event ...
} catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
}
```

### Q3：SocketEventHandle 的默认参数是什么？

```cpp
// 使用所有默认值
msgq::event::SocketEventHandle handle("socket_name");

// 自定义标识符前缀
msgq::event::SocketEventHandle handle("socket_name", "my_prefix");

// 覆盖现有事件处理器
msgq::event::SocketEventHandle handle("socket_name", "", true);

// 不覆盖现有处理器
msgq::event::SocketEventHandle handle("socket_name", "", false);
```

### Q4：如何处理多个 Event 的所有权？

**使用智能指针：**
```cpp
std::vector<std::unique_ptr<msgq::event::Event>> events;

events.push_back(
    std::make_unique<msgq::event::Event>(fd1)
);

// ✅ 作用域结束时自动释放
```

**或使用移动语义：**
```cpp
std::vector<msgq::event::Event> events;

auto event = msgq::event::Event(fd);
events.push_back(std::move(event));  // 转移所有权
```

### Q5：在线程中使用 Event 安全吗？

**是的，以下模式是安全的：**
```cpp
// 线程 1
msgq::event::Event event(eventfd(0, 0));

// 线程 2
event.set();  // ✅ 线程安全

// 主线程
event.wait();  // ✅ 等待 Event 的线程安全触发
```

**注意：** 不能从多个线程同时销毁 Event（但 RAII 保证了这不会发生）

---

## 性能对比

### 内存使用

| 操作 | 旧版本 | 新版本 | 差异 |
|------|--------|--------|------|
| Event 对象 | int fd | int fd + 元数据 | ~相同 |
| wait_for_one | 栈上 VLA | 堆上 std::vector | ✅ 可扩展 |
| SocketEventHandle | 手动管理 | RAII 守卫 | ~相同 |

### CPU 使用

| 操作 | 旧版本 | 新版本 | 差异 |
|------|--------|--------|------|
| set() | 一次 write() | 一次 write() | ✅ 相同 |
| wait() | poll/ppoll | poll/ppoll | ✅ 相同 |
| 错误检查 | 条件判断 | 异常处理 | ~相同 |

### 关键改进

- **栈溢出风险：** 消除（VLA → std::vector）
- **资源泄漏：** 消除（RAII）
- **异常安全：** 强异常安全（新）
- **代码简洁：** 改进（自动清理）

---

## 示例：完整的事件处理

### 旧版本

```cpp
#include "event.h"
#include <cstring>

void handle_socket_event() {
    // 创建事件处理器
    msgq::event::SocketEventHandle handle("test_socket", "", true);
    
    // 手动获取事件 - 容易出错
    auto recv_called = handle.recv_called();
    auto recv_ready = handle.recv_ready();
    
    try {
        // 触发事件
        recv_called.set();
        
        // 等待接收完成
        recv_ready.wait(5);
        
        std::cout << "Event handled successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        // ⚠️ 错误清理可能不完整
    }
    
    // ⚠️ 如果异常抛出，资源可能泄漏
}
```

### 新版本

```cpp
#include "event_modern.h"
#include <iostream>

void handle_socket_event() {
    try {
        // 创建事件处理器
        msgq::event::SocketEventHandle handle("test_socket", "", true);
        
        // 获取事件
        auto recv_called = handle.recv_called();
        auto recv_ready = handle.recv_ready();
        
        // 触发事件
        recv_called.set();
        
        // 等待接收完成
        recv_ready.wait(5);
        
        std::cout << "Event handled successfully" << std::endl;
        
        // ✅ 作用域结束时自动清理所有资源
        // - Event 对象销毁（fd 关闭）
        // - SocketEventHandle 销毁（munmap、unlink）
        // - 即使异常抛出，清理仍然执行
    } catch (const std::runtime_error& e) {
        std::cerr << "Wait timeout: " << e.what() << std::endl;
        // ✅ 所有资源自动正确清理
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        // ✅ 强异常安全保证
    }
}
```

---

## 集成建议

### 1. 创建别名以简化命名空间

```cpp
// 在项目命名空间中
namespace myproject {
    using Event = msgq::event::Event;
    using SocketEventHandle = msgq::event::SocketEventHandle;
}

// 使用
myproject::Event event(fd);
```

### 2. 创建便利函数

```cpp
namespace msgq::event {
    inline std::unique_ptr<Event> create_event() {
        int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (fd < 0) {
            throw std::runtime_error("Failed to create eventfd");
        }
        return std::make_unique<Event>(fd);
    }
}
```

### 3. 更新编译标志

```makefile
# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(myproject)

# ✅ 要求 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ✅ 启用警告
add_compile_options(-Wall -Wextra -Wshadow -Wformat=2)
```

---

## 验证清单

- [ ] 所有文件编译通过（-Wall -Wextra -Werror）
- [ ] 原有单元测试通过
- [ ] 内存检查通过（valgrind --leak-check=full）
- [ ] 线程检查通过（-fsanitize=thread）
- [ ] Python 绑定仍然工作
- [ ] 性能基准测试通过
- [ ] 代码审查通过
- [ ] 部署到生产环境

---

## 回滚计划

如果需要回滚到旧版本：

```bash
# 恢复包含
sed -i 's/#include "event_modern\.h"/#include "event.h"/g' *.cc *.h

# 恢复编译标志
# 编辑 CMakeLists.txt，将 CMAKE_CXX_STANDARD 改回 14 或 11

# 重新编译
make clean
make
```

---

## 支持和反馈

- 文档：见 EVENT_ANALYSIS.md
- 示例：见 event_modern.h 中的注释
- 问题报告：提交 GitHub issue
