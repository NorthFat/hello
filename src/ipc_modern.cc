#include "ipc_modern.h"

#include <cstdlib>
#include <cassert>
#include <iostream>
#include <map>

// Forward declarations - 实际的实现类在具体的 impl_*.h 中定义
// 这里我们只声明工厂函数的接口

namespace msgq {

// ============================================================================
// 配置和检测函数
// ============================================================================

bool messaging_use_zmq() noexcept {
    // 检查 ZMQ 环境变量
    if (std::getenv("ZMQ")) {
        // 如果指定了 OPENPILOT_PREFIX，ZMQ 后端无法支持
        if (std::getenv("OPENPILOT_PREFIX")) {
            std::cerr << "WARNING: OPENPILOT_PREFIX not supported with ZMQ backend\n";
            // 返回 true，表示必须使用 ZMQ
            return true;
        }
        return true;
    }

    // 在不支持 MSGQ 的平台上（macOS），必须使用 ZMQ
    if (!is_platform_supports_msgq()) {
        if (std::getenv("OPENPILOT_PREFIX")) {
            std::cerr << "ERROR: OPENPILOT_PREFIX requires Linux with MSGQ support\n";
        }
        return true;  // 强制使用 ZMQ
    }

    return false;  // 使用 MSGQ（默认）
}

bool messaging_use_fake() noexcept {
    return std::getenv("CEREAL_FAKE") != nullptr;
}

BackendType determine_backend_type() noexcept {
    const bool use_fake = messaging_use_fake();
    const bool use_zmq = messaging_use_zmq();

    if (use_fake) {
        return use_zmq ? BackendType::FAKE_ZMQ : BackendType::FAKE_MSGQ;
    } else {
        return use_zmq ? BackendType::ZMQ : BackendType::MSGQ;
    }
}

// ============================================================================
// 工厂方法实现
// ============================================================================

// 这些函数需要链接到具体的实现（impl_zmq.cc, impl_msgq.cc, impl_fake.cc）
// 前向声明：实现应该在各个实现文件中定义这些工厂函数

namespace detail {
    // 工厂函数类型定义
    using ContextFactory = std::function<std::unique_ptr<Context>()>;
    using SubSocketFactory = std::function<std::unique_ptr<SubSocket>()>;
    using PubSocketFactory = std::function<std::unique_ptr<PubSocket>()>;
    using PollerFactory = std::function<std::unique_ptr<Poller>()>;

    // 实现需要提供这些工厂函数
    extern std::unique_ptr<Context> create_zmq_context();
    extern std::unique_ptr<Context> create_msgq_context();
    extern std::unique_ptr<SubSocket> create_zmq_subsocket();
    extern std::unique_ptr<SubSocket> create_msgq_subsocket();
    extern std::unique_ptr<SubSocket> create_fake_zmq_subsocket();
    extern std::unique_ptr<SubSocket> create_fake_msgq_subsocket();
    extern std::unique_ptr<PubSocket> create_zmq_pubsocket();
    extern std::unique_ptr<PubSocket> create_msgq_pubsocket();
    extern std::unique_ptr<Poller> create_zmq_poller();
    extern std::unique_ptr<Poller> create_msgq_poller();
    extern std::unique_ptr<Poller> create_fake_poller();
}

// ============================================================================
// Context 工厂实现
// ============================================================================

std::unique_ptr<Context> Context::create() {
    try {
        const bool use_zmq = messaging_use_zmq();
        
        if (use_zmq) {
            return detail::create_zmq_context();
        } else {
            return detail::create_msgq_context();
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to create Context: ") + e.what()
        );
    }
}

// ============================================================================
// SubSocket 工厂实现
// ============================================================================

std::unique_ptr<SubSocket> SubSocket::create() {
    try {
        const BackendType backend_type = determine_backend_type();
        
        switch (backend_type) {
            case BackendType::FAKE_ZMQ:
                return detail::create_fake_zmq_subsocket();
            case BackendType::FAKE_MSGQ:
                return detail::create_fake_msgq_subsocket();
            case BackendType::ZMQ:
                return detail::create_zmq_subsocket();
            case BackendType::MSGQ:
                return detail::create_msgq_subsocket();
        }
        
        // 不应该到达这里
        throw std::runtime_error("Unknown backend type");
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to create SubSocket: ") + e.what()
        );
    }
}

std::unique_ptr<SubSocket> SubSocket::create(
    Context* context,
    const std::string& endpoint,
    const std::string& address,
    bool conflate,
    bool check_endpoint) {
    
    if (context == nullptr) {
        throw std::invalid_argument("Context pointer cannot be null");
    }
    
    if (endpoint.empty()) {
        throw std::invalid_argument("Endpoint cannot be empty");
    }

    try {
        auto socket = SubSocket::create();
        int r = socket->connect(context, endpoint, address, conflate, check_endpoint);
        
        if (r != 0) {
            throw std::runtime_error(
                "Failed to connect SubSocket to '" + endpoint + 
                "': " + std::string(std::strerror(errno))
            );
        }
        
        return socket;
    } catch (const std::exception& e) {
        if (check_endpoint) {
            throw;  // 重新抛出异常
        } else {
            // 如果不检查端点，只记录警告
            std::cerr << "WARNING: Failed to connect SubSocket: " << e.what() << "\n";
            return nullptr;
        }
    }
}

// ============================================================================
// PubSocket 工厂实现
// ============================================================================

std::unique_ptr<PubSocket> PubSocket::create() {
    try {
        const bool use_zmq = messaging_use_zmq();
        
        if (use_zmq) {
            return detail::create_zmq_pubsocket();
        } else {
            return detail::create_msgq_pubsocket();
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to create PubSocket: ") + e.what()
        );
    }
}

std::unique_ptr<PubSocket> PubSocket::create(
    Context* context,
    const std::string& endpoint,
    bool check_endpoint) {
    
    if (context == nullptr) {
        throw std::invalid_argument("Context pointer cannot be null");
    }
    
    if (endpoint.empty()) {
        throw std::invalid_argument("Endpoint cannot be empty");
    }

    try {
        auto socket = PubSocket::create();
        int r = socket->connect(context, endpoint, check_endpoint);
        
        if (r != 0) {
            throw std::runtime_error(
                "Failed to bind PubSocket to '" + endpoint + 
                "': " + std::string(std::strerror(errno))
            );
        }
        
        return socket;
    } catch (const std::exception& e) {
        throw;
    }
}

std::unique_ptr<PubSocket> PubSocket::create(
    Context* context,
    const std::string& endpoint,
    int port,
    bool check_endpoint) {
    
    if (context == nullptr) {
        throw std::invalid_argument("Context pointer cannot be null");
    }
    
    if (endpoint.empty()) {
        throw std::invalid_argument("Endpoint cannot be empty");
    }
    
    if (port < 0 || port > 65535) {
        throw std::invalid_argument("Port must be between 0 and 65535");
    }

    try {
        auto socket = PubSocket::create();
        int r = socket->connect(context, endpoint, check_endpoint);
        
        if (r != 0) {
            throw std::runtime_error(
                "Failed to bind PubSocket to '" + endpoint + 
                "' on port " + std::to_string(port) +
                ": " + std::string(std::strerror(errno))
            );
        }
        
        return socket;
    } catch (const std::exception& e) {
        throw;
    }
}

// ============================================================================
// Poller 工厂实现
// ============================================================================

std::unique_ptr<Poller> Poller::create() {
    try {
        const bool use_fake = messaging_use_fake();
        
        if (use_fake) {
            return detail::create_fake_poller();
        }
        
        const bool use_zmq = messaging_use_zmq();
        
        if (use_zmq) {
            return detail::create_zmq_poller();
        } else {
            return detail::create_msgq_poller();
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to create Poller: ") + e.what()
        );
    }
}

std::unique_ptr<Poller> Poller::create(
    const std::vector<SubSocket*>& sockets) {
    
    if (sockets.empty()) {
        throw std::invalid_argument("Socket list cannot be empty");
    }
    
    // 检查所有套接字都非 null
    for (size_t i = 0; i < sockets.size(); ++i) {
        if (sockets[i] == nullptr) {
            throw std::invalid_argument(
                "Socket at index " + std::to_string(i) + " is null"
            );
        }
    }

    try {
        auto poller = Poller::create();
        
        for (const auto& socket : sockets) {
            poller->registerSocket(socket);
        }
        
        return poller;
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to create and initialize Poller: ") + e.what()
        );
    }
}

} // namespace msgq
