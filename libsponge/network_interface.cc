#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "network_interface.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    // 将传入的 IP address 转换为 arp 报文头的下一跳的 ip 地址
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    // check arp cache table first
    if (_arp_table.find(next_hop_ip) != _arp_table.end()) {
        // arp_table hit 直接发送 以太网数据帧
        push_datagram(_arp_table[next_hop_ip].first, EthernetHeader::TYPE_IPv4, dgram.serialize());
    } else {
        // arp_table not hit, 并且最近没有对该 IP 发送过 ARP 查询报文
        // 发送广播报文, 查询对应 IP 地址的 MAC 地址
        if (_arp_request_time.find(next_hop_ip) == _arp_request_time.end()) {
            // 组装 ARP 查询报文
            ARPMessage arp_request;
            arp_request.opcode = ARPMessage::OPCODE_REQUEST;
            arp_request.sender_ethernet_address = _ethernet_address;
            arp_request.sender_ip_address = _ip_address.ipv4_numeric();
            arp_request.target_ethernet_address = {};
            arp_request.target_ip_address = next_hop_ip;
            push_datagram(ETHERNET_BROADCAST, EthernetHeader::TYPE_ARP, arp_request.serialize());

            // 还没有在 arp_wait_ipdata 队列中出现 ip 地址
            _arp_request_time.emplace(next_hop_ip, 0);
            _arp_wait_ipdata.insert({next_hop_ip, {dgram}});
        }
        // 在一定时间内已经发送过查询报文
        else {
            _arp_wait_ipdata[next_hop_ip].emplace_back(dgram);
        }
    }
}

// 接收到 以太网数据帧
//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // 如果目标地址不是本机或者不是广播帧
    if (!(frame.header().dst == _ethernet_address || frame.header().dst == ETHERNET_BROADCAST)) {
        return nullopt;
    }

    auto const &frame_type = frame.header().type;
    // 收到 IPV4 报文 parse 返回即可
    if (frame_type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram ip_data;
        if (ip_data.parse(frame.payload()) == ParseResult::NoError) {
            return ip_data;
        } else {
            return nullopt;
        }
    }

    // ARP 报文
    if (frame_type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_data;
        if (arp_data.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        }
        // ARP 报文即可更新 ARP cache table
        _arp_table[arp_data.sender_ip_address] = {arp_data.sender_ethernet_address, 0};

        // ARP request 报文, 需要发送 ARP reply报文
        if (arp_data.opcode == ARPMessage::OPCODE_REQUEST && arp_data.target_ip_address == _ip_address.ipv4_numeric()) {
            // 组装 arp reply 报文 发送
            ARPMessage arp_reply;
            arp_reply.opcode = ARPMessage::OPCODE_REPLY;
            arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            arp_reply.sender_ethernet_address = _ethernet_address;
            arp_reply.target_ip_address = arp_data.sender_ip_address;
            arp_reply.target_ethernet_address = arp_data.sender_ethernet_address;
            push_datagram(arp_data.sender_ethernet_address, EthernetHeader::TYPE_ARP, arp_reply.serialize());
        }

        // 将发送的 arp 报文的发送者 sender 在本机的等待 IP data 发送出去
        if (_arp_wait_ipdata.find(arp_data.sender_ip_address) != _arp_wait_ipdata.end()) {
            auto const &ip_data = _arp_wait_ipdata[arp_data.sender_ip_address];
            for (auto &data : ip_data) {
                push_datagram(arp_data.sender_ethernet_address, EthernetHeader::TYPE_IPv4, data.serialize());
            }
            _arp_wait_ipdata.erase(arp_data.sender_ip_address);
        }
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // tick time pass
    // delete arp cache table which deactive
    for (auto it = _arp_table.begin(); it != _arp_table.end();) {
        it->second.second += ms_since_last_tick;
        if (ARP_ENTRY_TTL_MS <= it->second.second) {
            it = _arp_table.erase(it);
        } else {
            it++;
        }
    }
    // delete arp_request_time 删除其中 request 超时的部分, 同时删除对应的 ip_data
    for (auto it = _arp_request_time.begin(); it != _arp_request_time.end();) {
        it->second += ms_since_last_tick;
        if (ARP_REPLY_TTL_MS <= it->second) {
            if (_arp_wait_ipdata.find(it->first) != _arp_wait_ipdata.end()) {
                _arp_wait_ipdata.erase(it->first);
            }
            it = _arp_request_time.erase(it);
        } else {
            it++;
        }
    }
}

//! 组装以太网帧结构
void NetworkInterface::push_datagram(const EthernetAddress &dst, const uint16_t type, BufferList &&payload) {
    EthernetFrame frame;
    frame.header().src = _ethernet_address;
    frame.header().dst = dst;
    frame.header().type = type;
    frame.payload() = std::move(payload);
    _frames_out.push(std::move(frame));
}