/**
   Copyright (c) 2015 Beckhoff Automation GmbH & Co. KG

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
 */

#include "Sockets.h"
#include "Log.h"

#include <algorithm>
#include <climits>
#include <exception>
#include <sstream>
#include <system_error>

IpV4::IpV4(const std::string& addr)
    : value(ntohl(inet_addr(addr.c_str())))
{}

IpV4::IpV4(uint32_t __val)
    : value(__val)
{}

bool IpV4::operator<(const IpV4& ref) const
{
    return value < ref.value;
}

bool IpV4::operator==(const IpV4& ref) const
{
    return value == ref.value;
}

Socket::Socket(IpV4 ip, uint16_t port, int type)
    : m_WSAInitialized(!InitSocketLibrary()),
    m_Socket(socket(AF_INET, type, 0)),
    m_DestAddr(SOCK_DGRAM == type ? reinterpret_cast<const struct sockaddr*>(&m_SockAddress) : nullptr),
    m_DestAddrLen(m_DestAddr ? sizeof(m_SockAddress) : 0)
{
    if (INVALID_SOCKET == m_Socket) {
        throw std::system_error(WSAGetLastError(), std::system_category());
    }
    m_SockAddress.sin_family = AF_INET;
    m_SockAddress.sin_port = htons(port);
    m_SockAddress.sin_addr.s_addr = htonl(ip.value);
}

Socket::~Socket()
{
    Shutdown();
    closesocket(m_Socket);

    if (m_WSAInitialized) {
        WSACleanup();
    }
}

void Socket::Shutdown()
{
    shutdown(m_Socket, SHUT_RDWR);
}

size_t Socket::read(uint8_t* buffer, size_t maxBytes) const
{
    if (!Select(nullptr)) {
        return 0;
    }

    maxBytes = static_cast<int>(std::min<size_t>(INT_MAX, maxBytes));
    const int bytesRead = recv(m_Socket, reinterpret_cast<char*>(buffer), maxBytes, 0);
    if (bytesRead > 0) {
        return bytesRead;
    }
    const auto lastError = WSAGetLastError();
    if ((0 == bytesRead) || (lastError == CONNECTION_CLOSED)) {
        throw std::runtime_error("connection closed by remote");
    } else {
        LOG_ERROR("read frame failed with error: " << std::dec << lastError);
    }
    return 0;
}

Frame& Socket::read(Frame& frame) const
{
    const size_t bytesRead = read(frame.rawData(), frame.capacity());
    if (bytesRead) {
        return frame.limit(bytesRead);
    }
    return frame.clear();
}

bool Socket::Select(timeval* timeout) const
{
    /* prepare socket set for select() */
    fd_set readSockets;
    FD_ZERO(&readSockets);
    FD_SET(m_Socket, &readSockets);

    /* wait for receive data */
    const int state = NATIVE_SELECT(m_Socket + 1, &readSockets, nullptr, nullptr, timeout);
    if (0 == state) {
        LOG_ERROR("select() timeout");
        return false;
    }

    const auto lastError = WSAGetLastError();
    if (lastError == WSAENOTSOCK) {
        throw std::runtime_error("connection closed");
    }

    /* and check if socket was correct */
    if ((1 != state) || (!FD_ISSET(m_Socket, &readSockets))) {
        LOG_ERROR(
            "something strange happen while waiting for socket... with error: " << lastError << " state: " << state);
        return false;
    }
    return true;
}

size_t Socket::write(const Frame& frame) const
{
    if (frame.size() > INT_MAX) {
        LOG_ERROR("frame length: " << frame.size() << " exceeds maximum length for sockets");
        return 0;
    }

    const int bufferLength = static_cast<int>(frame.size());
    const char* const buffer = reinterpret_cast<const char*>(frame.data());

    const int status = sendto(m_Socket, buffer, bufferLength, 0, m_DestAddr, m_DestAddrLen);
    if (SOCKET_ERROR == status) {
        LOG_ERROR("write frame failed with error: " << WSAGetLastError());
        return 0;
    }
    return status;
}

TcpSocket::TcpSocket(const IpV4 ip, const uint16_t port)
    : Socket(ip, port, SOCK_STREAM)
{
    // AdsDll.lib seems to use TCP_NODELAY, we use it to be compatible
    const int enable = 0;
    if (setsockopt(m_Socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&enable, sizeof(enable))) {
        LOG_WARN("Enabling TCP_NODELAY failed");
    }
}

uint32_t TcpSocket::Connect() const
{
    const uint32_t addr = ntohl(m_SockAddress.sin_addr.s_addr);
    LOG_INFO("Connecting to " << ((addr & 0xff000000) >> 24) << '.' << ((addr & 0xff0000) >> 16) << '.' <<
             ((addr & 0xff00) >> 8) << '.' << (addr & 0xff));

    if (::connect(m_Socket, reinterpret_cast<const sockaddr*>(&m_SockAddress), sizeof(m_SockAddress))) {
        LOG_ERROR("Connect TCP socket failed with: " << WSAGetLastError());
        return 0;
    }

    struct sockaddr_in source;
    socklen_t len = sizeof(source);

    if (getsockname(m_Socket, reinterpret_cast<sockaddr*>(&source), &len)) {
        LOG_ERROR("Read local tcp/ip address failed");
        return 0;
    }
    return ntohl(source.sin_addr.s_addr);
}

UdpSocket::UdpSocket(IpV4 ip, uint16_t port)
    : Socket(ip, port, SOCK_DGRAM)
{}
