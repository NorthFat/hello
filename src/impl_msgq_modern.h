#pragma once

#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

#include "msgq/ipc.h"
#include "msgq/msgq.h"

/// @file impl_msgq_modern.h
/// @brief MSGQ 后端的现代 C++17 实现
/// @details 替代原始 impl_msgq.h，提供：
///   - 完整的 RAII 资源管理
///   - 异常安全保证
///   - const 正确的 API
///   - 完整的 Doxygen 文档

#define MAX_POLLERS 128

/// @brief MSGQ 上下文（MSGQ 后端不需要单独的上下文）
class MSGQContext : public Context {
public:
  /// @brief 获取原始 ZMQ 上下文指针
  /// @return MSGQ 不使用上下文，返回 nullptr
  void* getRawContext() const override {
    return nullptr;
  }

  /// @brief 虚析构函数
  ~MSGQContext() override = default;
};

/// @brief MSGQ 消息实现（使用现代 C++ vector 管理内存）
class MSGQMessage : public Message {
private:
  std::vector<char> data;  ///< 消息数据，RAII 自动管理

public:
  /// @brief 初始化指定大小的消息缓冲区
  /// @param size 消息大小（字节）
  /// @throws std::bad_alloc 如果内存分配失败
  void init(size_t size) override;

  /// @brief 初始化并复制消息数据
  /// @param data 源数据指针（非空）
  /// @param size 数据大小
  /// @throws std::invalid_argument 如果 data 为空且 size > 0
  /// @throws std::bad_alloc 如果内存分配失败
  void init(char* data, size_t size) override;

  /// @brief 接管外部数据的所有权
  /// @details 将外部分配的数据所有权转移到此消息对象
  /// @param data 外部分配的数据指针
  /// @param size 数据大小
  /// @throws std::invalid_argument 如果 data 为空且 size > 0
  void takeOwnership(char* data, size_t size);

  /// @brief 获取消息大小（字节）
  /// @return 消息大小，0 表示空消息
  size_t getSize() const override {
    return data.size();
  }

  /// @brief 获取消息数据指针
  /// @return 数据指针，可能为 nullptr 如果消息为空
  char* getData() const override {
    return const_cast<char*>(data.data());
  }

  /// @brief 清理消息数据
  void close() override;

  /// @brief 虚析构函数 - 自动释放所有资源
  ~MSGQMessage() override = default;
};

/// @brief MSGQ 订阅套接字实现
class MSGQSubSocket : public SubSocket {
private:
  std::unique_ptr<msgq_queue_t> q;  ///< MSGQ 队列对象，unique_ptr 自动管理
  int timeout = -1;                 ///< 接收超时（毫秒）-1=无限等待

  /// @brief 安全清理队列资源
  void cleanup();

public:
  /// @brief 连接到订阅套接字
  /// @param context MSGQ 上下文（非空）
  /// @param endpoint 端点名称
  /// @param address 服务地址（必须是 127.0.0.1）
  /// @param conflate 是否只保留最新消息
  /// @param check_endpoint 是否检查端点有效性
  /// @return 0 成功，-1 失败
  /// @throws std::invalid_argument 如果参数无效
  /// @throws std::runtime_error 如果队列创建失败
  int connect(Context* context, std::string endpoint,
              std::string address = "127.0.0.1", bool conflate = false,
              bool check_endpoint = true) override;

  /// @brief 设置接收超时时间
  /// @param timeout 超时毫秒数，-1 表示无限等待
  void setTimeout(int timeout) override {
    this->timeout = timeout;
  }

  /// @brief 接收消息
  /// @param non_blocking 非阻塞模式
  /// @return 接收到的消息（unique_ptr），nullptr 表示无消息
  std::unique_ptr<Message> receive(bool non_blocking = false) override;

  /// @brief 获取原始 MSGQ 队列指针
  /// @return msgq_queue_t 指针（只读）
  void* getRawSocket() const override;

  /// @brief 虚析构函数 - 自动清理所有资源
  ~MSGQSubSocket() override;
};

/// @brief MSGQ 发布套接字实现
class MSGQPubSocket : public PubSocket {
private:
  std::unique_ptr<msgq_queue_t> q;  ///< MSGQ 队列对象

  /// @brief 安全清理队列资源
  void cleanup();

public:
  /// @brief 连接到发布套接字
  /// @param context MSGQ 上下文（非空）
  /// @param endpoint 端点名称
  /// @param check_endpoint 是否检查端点有效性
  /// @return 0 成功，-1 失败
  /// @throws std::invalid_argument 如果参数无效
  /// @throws std::runtime_error 如果队列创建失败
  int connect(Context* context, std::string endpoint,
              bool check_endpoint = true) override;

  /// @brief 发送消息对象
  /// @param message 消息指针（非空）
  /// @return 发送的字节数，-1 表示失败
  /// @throws std::invalid_argument 如果 message 为空
  int sendMessage(Message* message) override;

  /// @brief 发送原始数据
  /// @param data 数据指针
  /// @param size 数据大小
  /// @return 发送的字节数，-1 表示失败
  int send(char* data, size_t size) override;

  /// @brief 检查所有订阅者是否已更新
  /// @return true 如果所有订阅者都收到最新消息
  bool all_readers_updated() const override;

  /// @brief 虚析构函数 - 自动清理所有资源
  ~MSGQPubSocket() override;
};

/// @brief MSGQ 轮询器实现
class MSGQPoller : public Poller {
private:
  std::vector<SubSocket*> sockets;      ///< 已注册的套接字列表
  std::vector<msgq_pollitem_t> polls;   ///< 轮询项数组

public:
  /// @brief 注册套接字以供轮询
  /// @param socket 子套接字指针（非空）
  /// @throws std::invalid_argument 如果 socket 为空
  /// @throws std::runtime_error 如果超出最大轮询器数量
  void registerSocket(SubSocket* socket) override;

  /// @brief 对已注册的套接字进行轮询
  /// @param timeout 超时毫秒数，-1 表示无限等待
  /// @return 准备好的套接字列表
  std::vector<SubSocket*> poll(int timeout) override;

  /// @brief 虚析构函数 - 自动清理所有资源
  ~MSGQPoller() override = default;
};

