#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <map>
#include <ctime>
#include <stdexcept>
#include <iostream>
#include <cerrno>
#include <cstring>

#ifdef __APPLE__
#define CLOCK_BOOTTIME CLOCK_MONOTONIC
#endif

namespace msgq {

// ============================================================================
// 常量和配置
// ============================================================================

constexpr int MSG_MULTIPLE_PUBLISHERS = 100;

/// 后端类型枚举，替代嵌套 if
enum class BackendType {
    FAKE_ZMQ,   ///< Fake + ZMQ 组合
    FAKE_MSGQ,  ///< Fake + MSGQ 组合
    ZMQ,        ///< ZMQ 后端
    MSGQ        ///< MSGQ 后端
};

// ============================================================================
// 抽象接口
// ============================================================================

/// @brief 消息队列上下文抽象接口
class Context {
public:
    /// @brief 获取底层上下文指针（仅用于 C 互操作性）
    /// @return 底层上下文指针
    [[nodiscard]] virtual void* getRawContext() const = 0;

    /// @brief 工厂方法：创建适当的上下文实现
    /// @return 持有新创建上下文的 unique_ptr
    /// @throws std::runtime_error 如果后端初始化失败
    [[nodiscard]] static std::unique_ptr<Context> create();

    /// @brief 虚析构函数
    virtual ~Context() = default;

protected:
    Context() = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};

/// @brief 消息对象抽象接口
class Message {
public:
    /// @brief 初始化指定大小的消息缓冲区
    /// @param size 缓冲区大小（字节）
    /// @throws std::bad_alloc 如果分配失败
    virtual void init(size_t size) = 0;

    /// @brief 初始化消息并复制数据
    /// @param data 要复制的数据指针
    /// @param size 数据大小（字节）
    /// @throws std::bad_alloc 如果分配失败
    virtual void init(const char* data, size_t size) = 0;

    /// @brief 关闭消息并释放资源
    virtual void close() = 0;

    /// @brief 获取消息大小
    /// @return 消息大小（字节）
    [[nodiscard]] virtual size_t getSize() const = 0;

    /// @brief 获取消息数据指针
    /// @return 消息数据指针
    [[nodiscard]] virtual char* getData() const = 0;

    /// @brief 虚析构函数
    virtual ~Message() = default;

protected:
    Message() = default;
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;
};

/// @brief 订阅者套接字抽象接口
class SubSocket {
public:
    /// @brief 连接到消息队列
    /// @param context 消息队列上下文（非空）
    /// @param endpoint 端点名称
    /// @param address IP 地址（默认为 127.0.0.1）
    /// @param conflate 是否丢弃过期消息，只保留最新消息
    /// @param check_endpoint 是否检查端点的有效性
    /// @return 0 如果成功，错误码如果失败
    /// @throws std::invalid_argument 如果 context 为 nullptr
    /// @throws std::runtime_error 如果连接失败且 check_endpoint 为 true
    virtual int connect(
        Context* context,
        const std::string& endpoint,
        const std::string& address = "127.0.0.1",
        bool conflate = false,
        bool check_endpoint = true) = 0;

    /// @brief 设置接收超时
    /// @param timeout 超时时间（毫秒），-1 表示无限等待
    virtual void setTimeout(int timeout) = 0;

    /// @brief 从套接字接收消息
    /// @param non_blocking 非阻塞模式（true 时立即返回，无消息则返回 nullptr）
    /// @return 接收到的消息指针，或 nullptr 如果无消息
    /// @throws std::runtime_error 如果接收失败
    [[nodiscard]] virtual std::unique_ptr<Message> receive(bool non_blocking = false) = 0;

    /// @brief 获取底层套接字指针（仅用于 C 互操作性）
    /// @return 底层套接字指针
    [[nodiscard]] virtual void* getRawSocket() const = 0;

    /// @brief 工厂方法：创建适当的子套接字实现
    /// @return 持有新创建套接字的 unique_ptr
    /// @throws std::bad_alloc 如果分配失败
    [[nodiscard]] static std::unique_ptr<SubSocket> create();

    /// @brief 工厂方法：创建并连接子套接字
    /// @param context 消息队列上下文（非空）
    /// @param endpoint 端点名称
    /// @param address IP 地址（默认为 127.0.0.1）
    /// @param conflate 是否合并消息
    /// @param check_endpoint 是否检查端点有效性
    /// @return 连接的套接字，或 nullptr 如果连接失败
    /// @throws std::invalid_argument 如果参数无效
    /// @throws std::runtime_error 如果连接失败
    [[nodiscard]] static std::unique_ptr<SubSocket> create(
        Context* context,
        const std::string& endpoint,
        const std::string& address = "127.0.0.1",
        bool conflate = false,
        bool check_endpoint = true);

    /// @brief 虚析构函数
    virtual ~SubSocket() = default;

protected:
    SubSocket() = default;
    SubSocket(const SubSocket&) = delete;
    SubSocket& operator=(const SubSocket&) = delete;
};

/// @brief 发布者套接字抽象接口
class PubSocket {
public:
    /// @brief 连接到消息队列
    /// @param context 消息队列上下文（非空）
    /// @param endpoint 端点名称
    /// @param check_endpoint 是否检查端点的有效性
    /// @return 0 如果成功，错误码如果失败
    /// @throws std::invalid_argument 如果 context 为 nullptr
    /// @throws std::runtime_error 如果绑定失败
    virtual int connect(
        Context* context,
        const std::string& endpoint,
        bool check_endpoint = true) = 0;

    /// @brief 发送消息对象
    /// @param message 要发送的消息
    /// @return 发送的字节数，-1 表示失败
    /// @throws std::invalid_argument 如果 message 为 nullptr
    /// @throws std::runtime_error 如果发送失败
    virtual int sendMessage(Message* message) = 0;

    /// @brief 发送原始数据
    /// @param data 数据指针
    /// @param size 数据大小（字节）
    /// @return 发送的字节数，-1 表示失败
    /// @throws std::invalid_argument 如果参数无效
    /// @throws std::runtime_error 如果发送失败
    virtual int send(const char* data, size_t size) = 0;

    /// @brief 检查所有读者是否已读取最新消息
    /// @return true 如果所有读者都已读取，false 否则
    [[nodiscard]] virtual bool all_readers_updated() const = 0;

    /// @brief 工厂方法：创建发布者套接字
    /// @return 持有新创建套接字的 unique_ptr
    /// @throws std::bad_alloc 如果分配失败
    [[nodiscard]] static std::unique_ptr<PubSocket> create();

    /// @brief 工厂方法：创建并连接发布者套接字
    /// @param context 消息队列上下文（非空）
    /// @param endpoint 端点名称
    /// @param check_endpoint 是否检查端点有效性
    /// @return 连接的套接字，或 nullptr 如果连接失败
    /// @throws std::invalid_argument 如果参数无效
    /// @throws std::runtime_error 如果连接失败
    [[nodiscard]] static std::unique_ptr<PubSocket> create(
        Context* context,
        const std::string& endpoint,
        bool check_endpoint = true);

    /// @brief 工厂方法：创建并连接发布者套接字（指定端口）
    /// @param context 消息队列上下文（非空）
    /// @param endpoint 端点名称
    /// @param port ZMQ 后端的端口号
    /// @param check_endpoint 是否检查端点有效性
    /// @return 连接的套接字，或 nullptr 如果连接失败
    /// @throws std::invalid_argument 如果参数无效
    /// @throws std::runtime_error 如果连接失败
    [[nodiscard]] static std::unique_ptr<PubSocket> create(
        Context* context,
        const std::string& endpoint,
        int port,
        bool check_endpoint = true);

    /// @brief 虚析构函数
    virtual ~PubSocket() = default;

protected:
    PubSocket() = default;
    PubSocket(const PubSocket&) = delete;
    PubSocket& operator=(const PubSocket&) = delete;
};

/// @brief 事件轮询器抽象接口
class Poller {
public:
    /// @brief 注册套接字用于轮询
    /// @param socket 套接字指针（非空）
    /// @throws std::invalid_argument 如果 socket 为 nullptr
    virtual void registerSocket(SubSocket* socket) = 0;

    /// @brief 轮询已注册的套接字
    /// @param timeout 超时时间（毫秒），-1 表示无限等待
    /// @return 有消息可读的套接字列表
    /// @throws std::runtime_error 如果轮询失败
    [[nodiscard]] virtual std::vector<SubSocket*> poll(int timeout) = 0;

    /// @brief 工厂方法：创建轮询器
    /// @return 持有新创建轮询器的 unique_ptr
    /// @throws std::bad_alloc 如果分配失败
    [[nodiscard]] static std::unique_ptr<Poller> create();

    /// @brief 工厂方法：创建并注册多个套接字的轮询器
    /// @param sockets 要注册的套接字列表
    /// @return 持有新创建轮询器的 unique_ptr，已注册所有套接字
    /// @throws std::invalid_argument 如果套接字列表为空
    /// @throws std::bad_alloc 如果分配失败
    [[nodiscard]] static std::unique_ptr<Poller> create(
        const std::vector<SubSocket*>& sockets);

    /// @brief 虚析构函数
    virtual ~Poller() = default;

protected:
    Poller() = default;
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
};

// ============================================================================
// 配置函数
// ============================================================================

/// @brief 检查是否应使用 ZMQ 后端
/// @return true 如果配置了 ZMQ 或平台不支持 MSGQ，false 否则
/// @note 优先级：ZMQ 环境变量 > 平台限制 > MSGQ（默认）
[[nodiscard]] bool messaging_use_zmq() noexcept;

/// @brief 检查是否应使用虚假事件（用于测试）
/// @return true 如果配置了 CEREAL_FAKE 环境变量，false 否则
[[nodiscard]] bool messaging_use_fake() noexcept;

/// @brief 确定当前后端类型
/// @return 对应的后端类型枚举
[[nodiscard]] BackendType determine_backend_type() noexcept;

/// @brief 检查当前平台是否支持 MSGQ 后端
/// @return true 如果平台支持（Linux），false 否则（macOS）
[[nodiscard]] inline bool is_platform_supports_msgq() noexcept {
    #ifdef __APPLE__
        return false;  // macOS 不支持 eventfd
    #else
        return true;   // Linux 完全支持
    #endif
}

} // namespace msgq
