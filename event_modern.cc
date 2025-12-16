#include "event_modern.h"

namespace msgq::event {

// 静态成员初始化
thread_local bool SocketEventHandle::fake_events_enabled_ = false;
thread_local std::string SocketEventHandle::fake_prefix_ = "";

} // namespace msgq::event
