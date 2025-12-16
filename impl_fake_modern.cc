#include <cassert>
#include <cstring>
#include <iostream>
#include <cstdlib>
#include <cerrno>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

#include "msgq/impl_fake_modern.h"

// ============================================================================
// EventStateGuard 实现
// ============================================================================

// 头文件中已定义，此处不需要重复实现

// ============================================================================
// FakeSubSocket 模板实现
// ============================================================================

template<typename TSubSocket>
int FakeSubSocket<TSubSocket>::connect(
    Context* context, const std::string& endpoint,
    const std::string& address, bool conflate, bool check_endpoint) {
  // 参数验证
  if (!context) {
    throw std::invalid_argument("Context cannot be null");
  }

  if (endpoint.empty()) {
    throw std::invalid_argument("Endpoint cannot be empty");
  }

  try {
    // 获取前缀环境变量
    const char* cereal_prefix = std::getenv("CEREAL_FAKE_PREFIX");
    std::string identifier = cereal_prefix != nullptr ? 
                             std::string(cereal_prefix) : "";

    // 分配和验证 mmap
    char* mem = nullptr;
    event_state_shm_mmap(endpoint.c_str(), identifier.c_str(), &mem, nullptr);

    if (!mem) {
      throw std::runtime_error(
          "Failed to mmap event state for endpoint: " + endpoint);
    }

    // 使用 RAII 包装管理 mmap 内存
    // 这确保即使后续操作异常，内存也会被正确释放
    auto guard = std::make_shared<EventStateGuard>(mem);

    // 获取事件状态指针
    EventState* event_state = reinterpret_cast<EventState*>(mem);
    if (!event_state) {
      throw std::runtime_error("Invalid event state pointer");
    }

    // 创建事件对象（智能指针管理）
    auto recv_called = std::make_shared<Event>(
        event_state->fds[EventPurpose::RECV_CALLED]);
    auto recv_ready = std::make_shared<Event>(
        event_state->fds[EventPurpose::RECV_READY]);

    // 验证事件创建成功
    if (!recv_called || !recv_ready) {
      throw std::runtime_error("Failed to create Event objects");
    }

    // 调用父类 connect 方法
    int r = TSubSocket::connect(context, endpoint, address, conflate, check_endpoint);
    if (r != 0) {
      throw std::runtime_error(
          "Failed to connect TSubSocket to endpoint: " + endpoint +
          ", error: " + std::string(strerror(errno)));
    }

    // 只在完全成功时转移所有权
    // 这确保强异常安全保证
    this->state_guard = guard;
    this->recv_called = recv_called;
    this->recv_ready = recv_ready;
    this->state = std::make_shared<EventState>(*event_state);

    return 0;

  } catch (const std::exception& e) {
    // 异常时，所有智能指针自动释放
    // state_guard 会调用 ~EventStateGuard() 清理 mmap
    throw;
  }
}

// ============================================================================
// FakePoller 实现
// ============================================================================

// 现代版本的 FakePoller 完全在头文件中定义

// ============================================================================
// 模板显式实例化
// ============================================================================

// 声明显式实例化（如果需要）
// 这些行应该在编译系统中适当处理
// 下面是常见的实例化：

#include "msgq/impl_msgq.h"
#include "msgq/impl_zmq.h"

// 显式实例化：FakeSubSocket 包装 MSGQSubSocket
template class FakeSubSocket<MSGQSubSocket>;

// 显式实例化：FakeSubSocket 包装 ZMQSubSocket（如果可用）
#ifdef ENABLE_ZMQ
template class FakeSubSocket<ZMQSubSocket>;
#endif

