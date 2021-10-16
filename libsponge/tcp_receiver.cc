#include "tcp_receiver.hh"

#include <iostream>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    if (seg.header().syn) {
        _syn = true;
        _isn = seg.header().seqno;
    }
    // 如果已经收到过 syn，并且没有处理过带有 fin 的 seg，则接受 seg 进行处理
    if (_syn) {
        uint64_t index;
        if (seg.header().syn)
            index = 0;
        else
            index = unwrap(seg.header().seqno, _isn, _reassembler.stream_out().bytes_written() + 1) - 1;

        _reassembler.push_substring(seg.payload().copy(), index, seg.header().fin);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_syn)
        return std::nullopt;
    else {
        // std::cout << _reassembler.stream_out().bytes_written() << std::endl;
        return wrap(_reassembler.stream_out().bytes_written(), _isn) + 1 + _reassembler.stream_out().input_ended();
    }
}

size_t TCPReceiver::window_size() const { return _reassembler.stream_out().remaining_capacity(); }
