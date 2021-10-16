#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time - _segment_received_time; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (seg.header().rst) {
        // sets both the inbound and outbound streams to the error state and kills the connection permanently
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _active = false;
    } else {
        if (_listening && seg.header().syn)
            // 如果在 _listening 状态下收到一个 syn，就跑出 _listening 状态
            _listening = false;

        _receiver.segment_received(seg);
        if (_receiver.stream_out().input_ended() && !_has_set_linger_eventually)
            _linger_after_streams_finish = false;

        _segment_received_time = _time;

        if (seg.header().ack)
            // 只有 ack 消息，携带 ackno 和 win （提出需求 -- 对方需要的下一个字节的序号和接收窗口大小）
            _sender.ack_received(seg.header().ackno, seg.header().win);

        if (!_listening)
            _sender.fill_window();

        // 这里要产生一个只是用于回应接收的空 seg
        if (_sender.segments_out().size() == 0 && seg.length_in_sequence_space())
            // 没有需要发送的内容（没数据且没syn、fin），但对方的发送没有停止
            _sender.send_empty_segment();

        // 确保每次接收后 _sender 队列都清空
        while (_sender.segments_out().size() != 0) {
            TCPSegment seg_ = _sender.segments_out().front();
            _sender.segments_out().pop();
            if (seg_.header().fin) {
                // 如果发送帧中包含 fin，则检视接收是否已经停止，如果确实如此，则完全结束 connection 不需要延时
                if (_receiver.stream_out().input_ended())
                    _linger_after_streams_finish = false;
                else
                    _linger_after_streams_finish = true;
                _has_set_linger_eventually = true;
            }
            optional<WrappingInt32> ackno = _receiver.ackno();  // 自己需要的下一个字节的序号
            size_t win = _receiver.window_size();               // 自己接收窗口的大小
            seg_.header().ack = true;
            seg_.header().ackno = *ackno;
            seg_.header().win = win;

            _segments_out.push(seg_);
        }
    }
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t bytes_written = _sender.stream_in().write(data);
    _sender.fill_window();
    while (_sender.segments_out().size() != 0) {  // 这里将所有 sender 中的 TCPSegment 同样地修改 header 是否合理？
        TCPSegment seg_ = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (seg_.header().fin) {
            // 如果发送帧中包含 fin，则检视接收是否已经停止，如果确实如此，则完全结束 connection 不需要延时
            if (_receiver.stream_out().input_ended())
                _linger_after_streams_finish = false;
            else
                _linger_after_streams_finish = true;
            _has_set_linger_eventually = true;
        }
        // 如果自己需要接收字节， 那么就要在即将发送的 TCPSegment 加上相应的 header 项
        optional<WrappingInt32> ackno = _receiver.ackno();  // 自己需要的下一个字节的序号
        size_t win = _receiver.window_size();               // 自己接收窗口的大小
        if (ackno) {
            seg_.header().ack = true;
            seg_.header().ackno = *ackno;
            seg_.header().win = win;
        }
        _segments_out.push(seg_);
    }
    return bytes_written;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _sender.tick(ms_since_last_tick);
    // 确保超时重发后 _sender 队列都清空
    while (_sender.segments_out().size() != 0) {
        TCPSegment seg_ = _sender.segments_out().front();
        _sender.segments_out().pop();
        // 如果自己需要接收字节， 那么就要在即将发送的 TCPSegment 加上相应的 header 项
        optional<WrappingInt32> ackno = _receiver.ackno();  // 自己需要的下一个字节的序号
        size_t win = _receiver.window_size();               // 自己接收窗口的大小
        if (ackno) {
            seg_.header().ack = true;
            seg_.header().ackno = *ackno;
            seg_.header().win = win;
        }
        _segments_out.push(seg_);
    }
    _time += ms_since_last_tick;
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        // abort the connection
        while (!_segments_out.empty())
            _segments_out.pop();
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _active = false;
        // send a reset segment to the peer (an empty segment with the rst flag set)
        _sender.send_empty_segment();
        TCPSegment seg_ = _sender.segments_out().front();
        _sender.segments_out().pop();
        seg_.header().rst = true;
        _segments_out.push(seg_);
    }
    // end the connection cleanly if necessary
    bool _in_stream_fin_recv = _receiver.stream_out().input_ended();
    bool _out_stream_success = _sender.stream_in().eof() &&
                               _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2 &&
                               _sender.bytes_in_flight() == 0;

    if (_in_stream_fin_recv && _out_stream_success) {
        if (!_linger_after_streams_finish)
            _active = false;
        else {
            // linger: the connection is only done after enough time (10 * _cfg.rt timeout) has elapsed
            // since the last segment was received
            if (time_since_last_segment_received() >= 10 * _cfg.rt_timeout)
                _active = false;
        }
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    write("");
}

void TCPConnection::connect() {
    // sending a SYN segment
    _sender.fill_window();
    TCPSegment seg_ = _sender.segments_out().front();
    _sender.segments_out().pop();
    _segments_out.push(seg_);
    _listening = false;
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            _sender.send_empty_segment();
            TCPSegment seg_ = _sender.segments_out().front();
            _sender.segments_out().pop();
            seg_.header().rst = true;
            _segments_out.push(seg_);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
