#ifndef SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
#define SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH

#include "ethernet_frame.hh"
#include "tcp_over_ip.hh"
#include "tun.hh"

#include <optional>
#include <queue>

//! \brief A "network interface" that connects IP (the internet layer, or network layer)
//! with Ethernet (the network access layer, or link layer).

//! This module is the lowest layer of a TCP/IP stack
//! (connecting IP with the lower-layer network protocol,
//! e.g. Ethernet). But the same module is also used repeatedly
//! as part of a router: a router generally has many network
//! interfaces, and the router's job is to route Internet datagrams
//! between the different interfaces.

//! The network interface translates datagrams (coming from the
//! "customer," e.g. a TCP/IP stack or router) into Ethernet
//! frames. To fill in the Ethernet destination address, it looks up
//! the Ethernet address of the next IP hop of each datagram, making
//! requests with the [Address Resolution Protocol](\ref rfc::rfc826).
//! In the opposite direction, the network interface accepts Ethernet
//! frames, checks if they are intended for it, and if so, processes
//! the the payload depending on its type. If it's an IPv4 datagram,
//! the network interface passes it up the stack. If it's an ARP
//! request or reply, the network interface processes the frame
//! and learns or replies as necessary.

//  networinterface 完成网络层与链路层之间的转换
//  主要是实现 send recv 以及 tick 三个函数接口

class NetworkInterface {
  private:
    // MAC 地址 IP 地址 以及存储以太网帧的队列
    //! Ethernet (known as hardware, network-access-layer, or link-layer) address of the interface
    EthernetAddress _ethernet_address;

    //! IP (known as internet-layer or network-layer) address of the interface
    Address _ip_address;

    //! outbound queue of Ethernet frames that the NetworkInterface wants sent
    std::queue<EthernetFrame> _frames_out{};

    // support datastruct

    // ARP Cache 表
    // IP address EthernetAddress TTL
    std::unordered_map<uint32_t, std::pair<EthernetAddress, size_t>> _arp_table{};

    // 正在查询的 ARP 报文
    // IP address time
    std::unordered_map<uint32_t, size_t> _arp_request_time{};

    // 存储 发送的 MAC 地址未被确认的IP报文
    std::unordered_map<uint32_t, std::vector<InternetDatagram>> _arp_wait_ipdata{};

    // ARP Cache 表中, ARP ENTRY 有效时间 30s
    static constexpr uint32_t ARP_ENTRY_TTL_MS = 30000;
    // ARP 请求报文的默认等待时间 5s
    static constexpr uint32_t ARP_REPLY_TTL_MS = 5000;

  public:
    //! \brief Construct a network interface with given Ethernet (network-access-layer) and IP (internet-layer) addresses
    NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address);

    //! \brief Access queue of Ethernet frames awaiting transmission
    std::queue<EthernetFrame> &frames_out() { return _frames_out; }

    //! \brief Sends an IPv4 datagram, encapsulated in an Ethernet frame (if it knows the Ethernet destination address).

    //! Will need to use [ARP](\ref rfc::rfc826) to look up the Ethernet destination address for the next hop
    //! ("Sending" is accomplished by pushing the frame onto the frames_out queue.)
    void send_datagram(const InternetDatagram &dgram, const Address &next_hop);

    //! \brief Receives an Ethernet frame and responds appropriately.

    //! If type is IPv4, returns the datagram.
    //! If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
    //! If type is ARP reply, learn a mapping from the "sender" fields.
    std::optional<InternetDatagram> recv_frame(const EthernetFrame &frame);

    //! \brief Called periodically when time elapses
    void tick(const size_t ms_since_last_tick);

    // support function
    //! \brief 组装 EthernetFrame 报文并将其发送
    void push_datagram(const EthernetAddress &dst, const uint16_t type, BufferList &&payload);
};

#endif  // SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
