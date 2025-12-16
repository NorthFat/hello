#include <cassert>
#include <cstring>
#include <iostream>
#include <cstdlib>
#include <cerrno>
#include <string>
#include <vector>
#include <stdexcept>
#include <memory>

#include "msgq/impl_msgq_modern.h"

// ============================================================================
// MSGQMessage 实现
// ============================================================================

void MSGQMessage::init(size_t size) {
  try {
    data.resize(size);
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("Failed to allocate message: ") + e.what());
  }
}

void MSGQMessage::init(char* src_data, size_t size) {
  if (size > 0 && !src_data) {
    throw std::invalid_argument("Source data cannot be null when size > 0");
  }

  try {
    data.clear();
    if (size > 0) {
      data.assign(src_data, src_data + size);
    }
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("Failed to initialize message: ") + e.what());
  }
}

void MSGQMessage::takeOwnership(char* src_data, size_t size) {
  if (size > 0 && !src_data) {
    throw std::invalid_argument("Source data cannot be null when size > 0");
  }

  try {
    data.clear();
    if (size > 0) {
      // 首先复制数据
      data.assign(src_data, src_data + size);
      // 然后释放源指针
      delete[] src_data;
    }
  } catch (const std::exception& e) {
    // 异常发生时，确保源数据被释放
    if (size > 0) {
      delete[] src_data;
    }
    throw;
  }
}

void MSGQMessage::close() {
  data.clear();
  data.shrink_to_fit();
}

// ============================================================================
// MSGQSubSocket 实现
// ============================================================================

void MSGQSubSocket::cleanup() {
  if (q) {
    msgq_close_queue(q.get());
  }
}

int MSGQSubSocket::connect(Context* context, std::string endpoint,
                           std::string address, bool conflate,
                           bool check_endpoint) {
  // 参数验证
  if (!context) {
    throw std::invalid_argument("Context cannot be null");
  }

  if (endpoint.empty()) {
    throw std::invalid_argument("Endpoint cannot be empty");
  }

  if (address != "127.0.0.1") {
    throw std::invalid_argument("MSGQ backend only supports address 127.0.0.1, got: " + address);
  }

  try {
    // 创建队列对象
    q = std::make_unique<msgq_queue_t>();

    // 初始化队列
    int r = msgq_new_queue(q.get(), endpoint.c_str(), DEFAULT_SEGMENT_SIZE);
    if (r != 0) {
      q.reset();
      throw std::runtime_error("Failed to create MSGQ queue '" + endpoint + "': " +
                              std::string(strerror(errno)));
    }

    // 初始化为订阅者
    try {
      msgq_init_subscriber(q.get());
    } catch (...) {
      cleanup();
      throw;
    }

    // 设置冲突处理模式
    if (conflate) {
      q->read_conflate = true;
    }

    timeout = -1;
    return 0;

  } catch (const std::exception& e) {
    cleanup();
    throw;
  }
}

std::unique_ptr<Message> MSGQSubSocket::receive(bool non_blocking) {
  if (!q) {
    throw std::runtime_error("Socket not connected");
  }

  msgq_msg_t msg = {};

  int rc = msgq_msg_recv(&msg, q.get());

  // 非阻塞模式下的重试逻辑
  if (!non_blocking) {
    while (rc == 0) {
      msgq_pollitem_t items[1] = {};
      items[0].q = q.get();

      int poll_timeout = (timeout != -1) ? timeout : 100;

      int n = msgq_poll(items, 1, poll_timeout);
      rc = msgq_msg_recv(&msg, q.get());

      // 成功接收到数据
      if (n > 0 && rc > 0) {
        break;
      }

      // 超时或已设置超时
      if (timeout != -1) {
        break;
      }
    }
  }

  // 创建现代消息对象
  if (rc > 0) {
    auto message = std::make_unique<MSGQMessage>();
    message->takeOwnership(msg.data, msg.size);
    return message;
  }

  return nullptr;
}

void* MSGQSubSocket::getRawSocket() const {
  if (!q) {
    return nullptr;
  }
  return q.get();
}

MSGQSubSocket::~MSGQSubSocket() {
  cleanup();
  q.reset();
}

// ============================================================================
// MSGQPubSocket 实现
// ============================================================================

void MSGQPubSocket::cleanup() {
  if (q) {
    msgq_close_queue(q.get());
  }
}

int MSGQPubSocket::connect(Context* context, std::string endpoint,
                           bool check_endpoint) {
  // 参数验证
  if (!context) {
    throw std::invalid_argument("Context cannot be null");
  }

  if (endpoint.empty()) {
    throw std::invalid_argument("Endpoint cannot be empty");
  }

  try {
    // 创建队列对象
    q = std::make_unique<msgq_queue_t>();

    // 初始化队列
    int r = msgq_new_queue(q.get(), endpoint.c_str(), DEFAULT_SEGMENT_SIZE);
    if (r != 0) {
      q.reset();
      throw std::runtime_error("Failed to create MSGQ queue '" + endpoint + "': " +
                              std::string(strerror(errno)));
    }

    // 初始化为发布者
    try {
      msgq_init_publisher(q.get());
    } catch (...) {
      cleanup();
      throw;
    }

    return 0;

  } catch (const std::exception& e) {
    cleanup();
    throw;
  }
}

int MSGQPubSocket::sendMessage(Message* message) {
  if (!message) {
    throw std::invalid_argument("Message cannot be null");
  }

  if (!q) {
    throw std::runtime_error("Socket not connected");
  }

  msgq_msg_t msg = {};
  msg.data = message->getData();
  msg.size = message->getSize();

  int result = msgq_msg_send(&msg, q.get());
  if (result < 0) {
    throw std::runtime_error("Failed to send message: " + std::string(strerror(errno)));
  }

  return result;
}

int MSGQPubSocket::send(char* data, size_t size) {
  if (!data && size > 0) {
    throw std::invalid_argument("Data cannot be null when size > 0");
  }

  if (!q) {
    throw std::runtime_error("Socket not connected");
  }

  msgq_msg_t msg = {};
  msg.data = data;
  msg.size = size;

  int result = msgq_msg_send(&msg, q.get());
  if (result < 0) {
    throw std::runtime_error("Failed to send data: " + std::string(strerror(errno)));
  }

  return result;
}

bool MSGQPubSocket::all_readers_updated() const {
  if (!q) {
    return false;
  }

  return msgq_all_readers_updated(q.get());
}

MSGQPubSocket::~MSGQPubSocket() {
  cleanup();
  q.reset();
}

// ============================================================================
// MSGQPoller 实现
// ============================================================================

void MSGQPoller::registerSocket(SubSocket* socket) {
  if (!socket) {
    throw std::invalid_argument("Socket cannot be null");
  }

  if (polls.size() >= MAX_POLLERS) {
    throw std::runtime_error("Maximum number of pollers (" + 
                            std::to_string(MAX_POLLERS) + ") exceeded");
  }

  msgq_pollitem_t item = {};
  item.q = static_cast<msgq_queue_t*>(socket->getRawSocket());

  if (!item.q) {
    throw std::invalid_argument("Socket getRawSocket() returned null");
  }

  polls.push_back(item);
  sockets.push_back(socket);
}

std::vector<SubSocket*> MSGQPoller::poll(int timeout) {
  std::vector<SubSocket*> ready;

  if (polls.empty()) {
    return ready;
  }

  // 调用 C 风格的轮询函数
  int rc = msgq_poll(polls.data(), polls.size(), timeout);

  if (rc < 0) {
    throw std::runtime_error("msgq_poll failed: " + std::string(strerror(errno)));
  }

  // 收集准备好的套接字
  for (size_t i = 0; i < polls.size(); ++i) {
    if (polls[i].revents) {
      ready.push_back(sockets[i]);
    }
  }

  return ready;
}

