#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

#include <iostream>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) 
    , _timer(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const { return _out_seqnos; }

void TCPSender::fill_window() {
    uint16_t ws = _window_size;
    // 如果窗口不足以填入下一个要发送的字节，则什么都不做，除非需要接受的字节恰好就是下一个要发送的字节
    if(_window_size <= (_next_seqno - _ack_seqno)){
        if(_next_seqno == _ack_seqno)
            ws = 1; 
        else
            return;
    }

    // 创建 TCPSegment
    TCPSegment seg;
    // 装入 header
    seg.header().seqno = next_seqno();   // 将下一个要发送的绝对序列号转换成相对序列号
    if(!_syn_sent){
        seg.header().syn = true;
        _syn_sent = true;
    }
    // 装入 payload
    string send_msg;
    uint16_t payload_size = ws - (_next_seqno - _ack_seqno) - seg.header().syn; // 确定发送内容的大小
    if(payload_size < TCPConfig::MAX_PAYLOAD_SIZE)
        send_msg = _stream.read(payload_size);
    else
        send_msg = _stream.read(TCPConfig::MAX_PAYLOAD_SIZE);
    if(_stream.bytes_read() == _stream.bytes_written() && _stream.eof() && payload_size > send_msg.size() && !_fin_sent){
        seg.header().fin = true;
        _fin_sent = true;
    }
    seg.payload() = Buffer(std::move(send_msg));   // 转换为右值引用，构造完之后 send_msg 就空了

    // 如果该 TCPSegment 序列长度不为零（没读到序列长度就可能为零），就发送这个 TCPSegment
    if(seg.length_in_sequence_space() > 0){
        _segments_out.push(seg);
        _next_seqno += seg.length_in_sequence_space();

        // 将 TCPSegment 放入 _out_segs 中
        OutstandingSegment out_seg;
        out_seg.seg = seg;
        out_seg.biggest_absolute_seqno = _next_seqno - 1;
        _out_segs.push_back(out_seg);
        _out_seqnos += seg.length_in_sequence_space();
        // 如果没有正在计时，就开始计时
        if(!_timer.is_started())
            _timer.start();
    }
    // 如果窗口没有填满，就继续填
    // 窗口是否有空的判断在 fill_window() 函数的最前面
    if(!_fin_sent && _stream.bytes_read() < _stream.bytes_written())
        fill_window();
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    _ack_seqno = unwrap(ackno, _isn, _next_seqno); // 下一个需要发送的字节序号
    _window_size = window_size;
    vector<OutstandingSegment>::iterator iter = _out_segs.begin();
    size_t size_of_out_segs = _out_segs.size();
    while(iter != _out_segs.end()){
        if (iter->biggest_absolute_seqno < _ack_seqno){
            _out_seqnos -= iter->seg.length_in_sequence_space();  // fly 序列数作出相应的调整
            iter = _out_segs.erase(iter); // 删除 _out_segs 中序列号小于 _next_seqno 的 TCPSegment
        } 
        else
            iter++;
    }
    // ack 恢复正常(_out_segs 中有 seg 被承认接收)，_timer 重开，_consecutive_retransmissions 归零
    if (size_of_out_segs > _out_segs.size()) {
        _timer.rto() = _initial_retransmission_timeout;
        if(_out_segs.size() != 0)
            _timer.restart();
        else
            _timer.close();
        _consecutive_retransmissions = 0;
    }

    // fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // 如果 _timer 处于开启状态，则计时
    // std::cout << _timer.is_started() << std::endl;
    if(_timer.is_started()) {
        _timer.increase(ms_since_last_tick);
        // std::cout << _timer.ticks() << std::endl;
        // std::cout << _timer.rto() << std::endl;
        if(_timer.is_expired()){
            _segments_out.push(_out_segs[0].seg); // 将最早的 TCPSegment 进行重传
            if (_window_size != 0 || _out_segs[0].seg.header().syn){ 
                // 如果收到过 ack，并且 _window_size=0，表明超时未收到 fly bytes ack 的原因可能是因为接收方数据
                // 处理不过来，不是网络问题，这个时候没必要将 rto 翻倍
                _consecutive_retransmissions ++;
                _timer.rto() += _timer.rto();
            }
            _timer.restart();
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    // 创建 TCPSegment
    TCPSegment seg;
    // seg.header().seqno = wrap(, _isn);

    // 发送 TCPSegment
    _segments_out.push(seg);
}
