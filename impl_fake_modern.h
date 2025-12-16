#pragma once

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "msgq/ipc.h"
#include "msgq/event.h"

/// @file impl_fake_modern.h
/// @brief Fake/test 后端的现代 C++17 实现
/// @details 用于单元和集成测试，提供：
///   - 事件同步机制
///   - 完全的内存安全（智能指针）
///   - 异常安全保证
///   - const 正确的 API

/// @brief EventState 内存映射的 RAII 包装
class EventStateGuard {
private:
  char* mem = nullptr;
  size_t size = sizeof(EventState);

public:
  /// @brief 构造函数（接管现有内存）
  /// @param memory 已映射的内存指针
  explicit EventStateGuard(char* memory) : mem(memory) {}

  /// @brief 析构函数（释放 mmap 内存）
  ~EventStateGuard() {
    cleanup();
  }

  /// @brief 禁止复制
  EventStateGuard(const EventStateGuard&) = delete;
  EventStateGuard& operator=(const EventStateGuard&) = delete;

  /// @brief 允许移动
  EventStateGuard(EventStateGuard&& other) noexcept
      : mem(other.mem), size(other.size) {
    other.mem = nullptr;
  }

  EventStateGuard& operator=(EventStateGuard&& other) noexcept {
    if (this != &other) {
      cleanup();
      mem = other.mem;
      size = other.size;
      other.mem = nullptr;
    }
    return *this;
  }

  /// @brief 获取原始指针
  /// @return EventState 指针
  char* get() const { return mem; }

  /// @brief 清理 mmap 内存
  void cleanup() {
    if (mem) {
      int rc = munmap(mem, size);
      if (rc != 0) {
        std::cerr << "Warning: munmap failed: " << strerror(errno) << std::endl;
      }
      mem = nullptr;
    }
  }
};

/// @brief Fake 订阅套接字（用于测试）
/// @tparam TSubSocket 真实的子套接字实现（ZMQSubSocket 或 MSGQSubSocket）
template<typename TSubSocket>
class FakeSubSocket : public TSubSocket {
private:
  /// @brief 接收已调用事件
  std::shared_ptr<Event> recv_called;

  /// @brief 接收准备事件
  std::shared_ptr<Event> recv_ready;

  /// @brief 事件状态的 RAII 管理
  std::shared_ptr<EventStateGuard> state_guard;

  /// @brief 事件状态指针（可变）
  mutable std::shared_ptr<EventState> state;

public:
  /// @brief 默认构造函数
  FakeSubSocket() : TSubSocket() {}

  /// @brief 虚析构函数
  /// @details 所有资源由智能指针自动释放
  ~FakeSubSocket() override = default;

  /// @brief 连接到 fake 套接字
  /// @param context 消息队列上下文
  /// @param endpoint 端点名称
  /// @param address 服务地址
  /// @param conflate 是否只保留最新消息
  /// @param check_endpoint 是否检查端点有效性
  /// @return 0 成功，-1 失败
  /// @throws std::invalid_argument 如果参数无效
  /// @throws std::runtime_error 如果内存映射或连接失败
  int connect(Context* context, const std::string& endpoint,
              const std::string& address = "127.0.0.1",
              bool conflate = false, bool check_endpoint = true) override;

  /// @brief 接收消息（带事件同步）
  /// @param non_blocking 非阻塞模式
  /// @return 接收到的消息，nullptr 表示无消息
  /// @details 如果启用 fake 事件，此方法会：
  ///   1. 设置 recv_called 事件
  ///   2. 等待 recv_ready 事件
  ///   3. 清除 recv_ready 事件
  ///   4. 然后调用底层 receive()
  Message* receive(bool non_blocking = false) override {
    if (state && state->enabled) {
      if (recv_called) {
        recv_called->set();
      }
      if (recv_ready) {
        recv_ready->wait();
        recv_ready->clear();
      }
    }

    return TSubSocket::receive(non_blocking);
  }
};

/// @brief Fake 轮询器（用于测试）
class FakePoller : public Poller {
private:
  /// @brief 已注册的套接字列表
  std::vector<SubSocket*> sockets;

public:
  /// @brief 注册套接字以供轮询
  /// @param socket 子套接字指针（非空）
  /// @throws std::invalid_argument 如果 socket 为空
  void registerSocket(SubSocket* socket) override {
    if (!socket) {
      throw std::invalid_argument("Socket cannot be null");
    }
    sockets.push_back(socket);
  }

  /// @brief 对已注册的套接字进行轮询
  /// @param timeout 超时毫秒数
  /// @return 准备好的套接字列表
  /// @details Fake 轮询器直接返回所有已注册的套接字
  std::vector<SubSocket*> poll(int timeout) const override {
    return sockets;
  }

  /// @brief 虚析构函数
  ~FakePoller() override = default;
};

