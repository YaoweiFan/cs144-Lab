#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

bool NetworkInterface::_arp_can_send(const uint32_t next_hop_ip) {
    if (_arp_time_map.find(next_hop_ip) != _arp_time_map.end()) {
        if (_arp_time_map[next_hop_ip] >= ARP_RETRANSMISSION_TIME) {
            _arp_time_map[next_hop_ip] = _arp_time_map[next_hop_ip] % ARP_RETRANSMISSION_TIME;
            return true;
        } else
            return false;
    } else {
        _arp_time_map[next_hop_ip] = 0;
        return true;
    }
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    if (_ip_mac_map.find(next_hop_ip) != _ip_mac_map.end()) {
        // 填充 ethernet 帧
        EthernetFrame ethernet_frame;
        ethernet_frame.header().type = EthernetHeader::TYPE_IPv4;
        ethernet_frame.payload() = dgram.serialize();
        ethernet_frame.header().src = _ethernet_address;
        ethernet_frame.header().dst = _ip_mac_map[next_hop_ip].first;
        _frames_out.push(ethernet_frame);
    } else {
        if (!_arp_can_send(next_hop_ip)) {
            // 将该 ip 帧压入队列
            _ip_frame_wait.push_back(std::pair(dgram, next_hop_ip));
            return;
        }
        // 广播一个 ARP 查询消息
        ARPMessage arp_msg;
        // 填充 ARPMessage
        arp_msg.opcode = ARPMessage::OPCODE_REQUEST;
        arp_msg.sender_ethernet_address = _ethernet_address;
        arp_msg.sender_ip_address = _ip_address.ipv4_numeric();
        arp_msg.target_ip_address = next_hop_ip;
        // 填充 ethernet 帧
        EthernetFrame ethernet_frame;
        ethernet_frame.header().type = EthernetHeader::TYPE_ARP;
        ethernet_frame.payload() = arp_msg.serialize();
        ethernet_frame.header().src = _ethernet_address;
        ethernet_frame.header().dst = ETHERNET_BROADCAST;
        _frames_out.push(ethernet_frame);
        // 将该 ip 帧压入队列
        _ip_frame_wait.push_back(std::pair(dgram, next_hop_ip));
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // 如果该 mac frame 的地址不是本机的 mac 地址，也不是广播地址，就直接 return
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST)
        return std::nullopt;

    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        if (dgram.parse(frame.payload()) == ParseResult::NoError)
            return dgram;
    }
    if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_msg;
        if (arp_msg.parse(frame.payload()) == ParseResult::NoError) {
            EthernetAddress sender_ethernet_address = arp_msg.sender_ethernet_address;
            uint32_t sender_ip_address = arp_msg.sender_ip_address;
            // 如果尚无该映射，存入映射表
            if (_ip_mac_map.find(sender_ip_address) == _ip_mac_map.end()) {
                _ip_mac_map[sender_ip_address] = std::pair(sender_ethernet_address, 0);
                // 清空相应的 ARP
                std::map<uint32_t, size_t>::iterator iter_arp_time_map = _arp_time_map.begin();
                while (iter_arp_time_map != _arp_time_map.end()) {
                    if (iter_arp_time_map->first == sender_ip_address)
                        iter_arp_time_map = _arp_time_map.erase(iter_arp_time_map);
                    else
                        iter_arp_time_map++;
                }
                // 对于 _ip_frame_wait 中需要该 mac 地址的 ip frame 进行处理
                std::vector<std::pair<InternetDatagram, uint32_t>>::iterator iter_ip_frame_wait =
                    _ip_frame_wait.begin();
                while (iter_ip_frame_wait != _ip_frame_wait.end()) {
                    if (iter_ip_frame_wait->second == sender_ip_address) {
                        InternetDatagram dgram = iter_ip_frame_wait->first;
                        EthernetFrame ethernet_frame;
                        ethernet_frame.header().type = EthernetHeader::TYPE_IPv4;
                        ethernet_frame.payload() = dgram.serialize();
                        ethernet_frame.header().src = _ethernet_address;
                        ethernet_frame.header().dst = _ip_mac_map[sender_ip_address].first;
                        _frames_out.push(ethernet_frame);
                        iter_ip_frame_wait = _ip_frame_wait.erase(iter_ip_frame_wait);
                    } else
                        iter_ip_frame_wait++;
                }
            }
            // 如果查询的是自己的 ip，就回复
            // ARP request 是广播，ARP reply 是定向的
            if (arp_msg.opcode == ARPMessage::OPCODE_REQUEST &&
                arp_msg.target_ip_address == _ip_address.ipv4_numeric()) {
                // 广播一个 ARP 回复消息
                ARPMessage arp_msg_reply;
                // 填充 ARPMessage
                arp_msg_reply.opcode = ARPMessage::OPCODE_REPLY;
                arp_msg_reply.sender_ethernet_address = _ethernet_address;
                arp_msg_reply.sender_ip_address = _ip_address.ipv4_numeric();
                arp_msg_reply.target_ethernet_address = arp_msg.sender_ethernet_address;
                arp_msg_reply.target_ip_address = arp_msg.sender_ip_address;
                // 填充 ethernet 帧
                EthernetFrame ethernet_frame;
                ethernet_frame.header().type = EthernetHeader::TYPE_ARP;
                ethernet_frame.payload() = arp_msg_reply.serialize();
                ethernet_frame.header().src = _ethernet_address;
                ethernet_frame.header().dst = arp_msg.sender_ethernet_address;
                _frames_out.push(ethernet_frame);
            }
        }
    }
    return std::nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    std::map<uint32_t, std::pair<EthernetAddress, size_t>>::iterator iter_ip_mac_map = _ip_mac_map.begin();
    while (iter_ip_mac_map != _ip_mac_map.end()) {
        iter_ip_mac_map->second.second += ms_since_last_tick;
        if (iter_ip_mac_map->second.second >= ip_mac_map_maintain_time)
            iter_ip_mac_map = _ip_mac_map.erase(iter_ip_mac_map);
        else
            iter_ip_mac_map++;
    }
    std::map<uint32_t, size_t>::iterator iter_arp_time_map = _arp_time_map.begin();
    while (iter_arp_time_map != _arp_time_map.end()) {
        iter_arp_time_map->second += ms_since_last_tick;
        iter_arp_time_map++;
    }
    return;
}
