#pragma once

namespace lwip
{
	/// @brief 初始化 TCP/IP 协议栈。
	/// @note 本函数幂等。
	void TcpIpInitialize();
} // namespace lwip
