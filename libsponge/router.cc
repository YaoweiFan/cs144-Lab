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

    Route r;
    r.route_prefix = route_prefix;
    r.prefix_length = prefix_length;
    r.next_hop = next_hop;
    r.interface_num = interface_num;
    _route_vec.push_back(r);
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    pair<int, size_t> max_length_and_index(pair(-1, 0));
    for (size_t i = 0; i < _route_vec.size(); i++) {
        uint64_t rp = _route_vec[i].route_prefix;
        rp = rp >> (32 - _route_vec[i].prefix_length);
        uint64_t dp = dgram.header().dst;
        dp = dp >> (32 - _route_vec[i].prefix_length);
        if (rp == dp && _route_vec[i].prefix_length > max_length_and_index.first) {
            max_length_and_index.first = _route_vec[i].prefix_length;
            max_length_and_index.second = i;
        }
    }
    if (max_length_and_index.first == -1)
        return;
    if (dgram.header().ttl == 0)
        return;
    else {
        dgram.header().ttl -= 1;
        if (dgram.header().ttl == 0)
            return;
    }
    size_t interface_num = _route_vec[max_length_and_index.second].interface_num;
    std::optional<Address> next_hop = _route_vec[max_length_and_index.second].next_hop;
    if (!next_hop)
        interface(interface_num).send_datagram(dgram, Address::from_ipv4_numeric(dgram.header().dst));
    else
        interface(interface_num).send_datagram(dgram, *next_hop);
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
