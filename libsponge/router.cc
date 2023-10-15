#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // DUMMY_CODE(route_prefix, prefix_length, next_hop, interface_num);
    // Your code here.
    _route_table.insert({route_prefix, prefix_length, next_hop, interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // DUMMY_CODE(dgram);
    // Your code here.
    // 从 ip 数据报文中 get ip address
    auto ip = dgram.header().dst;

    std::optional<RouteEntry> best_route{nullopt};

    // 在路由表中做最长前缀匹配
    for (auto const &route : _route_table) {
        // 子网掩码, 注意 _prefix_leng
        uint32_t mask = (route.get_prefix_length() == 0)
                            ? numeric_limits<uint32_t>::min()
                            : numeric_limits<uint32_t>::max() << (32 - route._prefix_length);
        // 最长前缀匹配
        if ((route.get_route_prefix() ^ (mask & ip)) == 0) {
            if (!best_route.has_value() ||
                (best_route.has_value() && (route.get_prefix_length() > best_route.value().get_prefix_length()))) {
                best_route = route;
            }
        }
    }

    // TTL <= 1 不会转发
    if (dgram.header().ttl <= 1) {
        return;
    }
    // 未匹配到路由规则
    if (!best_route.has_value()) {
        return;
    }

    --dgram.header().ttl;

    auto &next_interface = interface(best_route.value().get_interface_num());

    // 从路由表中得到的表项中, 下一跳有可能值为空 nullopt
    // 若为空, 表示路由器连接在对应的子网上, 下一跳就是 target ip
    // 否则为 下一跳 路由器的 ip 地址
    auto const &next_ip = best_route.value().get_next_op().has_value() ? best_route.value().get_next_op().value()
                                                                       : Address::from_ipv4_numeric(ip);

    next_interface.send_datagram(dgram, next_ip);
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
