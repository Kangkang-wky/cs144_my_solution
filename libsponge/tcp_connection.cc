#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

// time since last segment receive
size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

// 接收数据包
// 收到 segment 分成两个部分 payload 交给 receiver
// ackno 以及 window_size 交给
void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!active()) {
        return;
    }

    _time_since_last_segment_received = 0;

    const TCPHeader &header = seg.header();

    // 收到 RST 标识, 强制退出
    if (header.rst) {
        set_shutdown(false);
        return;
    }

    // receiver 处理 payload
    _receiver.segment_received(seg);

    // 收到 ack 标识, sender 处理 ackno, window_size
    if (header.ack) {
        _sender.ack_received(header.ackno, header.win);
    }

    // 收到 SYN 标识, 建立 tcp 连接
    if (header.syn && _sender.next_seqno_absolute() == 0) {
        connect();
        return;
    }

    // CLOSE_WAIT 表示满足 prerequest 1-3 置位
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED) {
        _linger_after_streams_finish = false;
    }

    // Keep-alive
    // from lab tutorial 可能包含非法的 seqno 来保持 tcp 连接, lab 中给出的状态表达式
    if (_receiver.ackno().has_value() && (seg.length_in_sequence_space() == 0) &&
        header.seqno == _receiver.ackno().value() - 1) {
        _sender.send_empty_segment();
    }

    // 如果收到的TCP segment 有占位空间, 则必须返回空的序列包
    if (seg.length_in_sequence_space() > 0 && _sender.segments_out().empty()) {
        _sender.fill_window();
        if (_sender.segments_out().empty()) {
            _sender.send_empty_segment();
        }
    }

    // 将 sender 和 receiver 组装好的 TCP 发送出去
    segment_assemble_send();
}

bool TCPConnection::active() const { return _is_active; }

size_t TCPConnection::write(const string &data) {
    size_t len = _sender.stream_in().write(data);
    // sender push 到自己的成员 segment_out 中
    _sender.fill_window();
    // sender receiver 一起完成对 Connection segment_out 的组装
    segment_assemble_send();
    return len;
}

// time tick passing
//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    // tick
    _sender.tick(ms_since_last_tick);
    _time_since_last_segment_received += ms_since_last_tick;

    // 情况一: 重传次数超过约定上限
    if (TCPConfig::MAX_RETX_ATTEMPTS < _sender.consecutive_retransmissions()) {
        // 清空发送队列
        while (!_sender.segments_out().empty()) {
            _sender.segments_out().pop();
        }
        // send a RST segment to the peer
        set_shutdown(false);
        send_rst_segment();
        return;
    }

    // tick 时间内有新的 segment 需要发送
    segment_assemble_send();

    // 满足三个前置条件 prereq 1 & 2 & 3
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED) {
        // 需要等待 默认为 true
        if (_linger_after_streams_finish) {
            if (_time_since_last_segment_received >= 10 * _cfg.rt_timeout) {
                set_shutdown(true);
            }
        }
        // 如果TCPConnection的入站流在TCPConnection发送FIN段之前就结束了，那么TCPConnection就不需要在两个流结束后等待。
        else {
            set_shutdown(true);
        }
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    segment_assemble_send();
}

void TCPConnection::connect() {
    _sender.fill_window();
    segment_assemble_send();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // 情况二: 在 TCP Connection active 的时候调用析构函数
            // Your code here: need to send a RST segment to the peer
            set_shutdown(false);
            send_rst_segment();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

// 发送数据包
void TCPConnection::segment_assemble_send() {
    TCPSegment segment;
    // sender part
    while (!_sender.segments_out().empty()) {
        segment = _sender.segments_out().front();
        _sender.segments_out().pop();

        // receiver part
        if (_receiver.ackno().has_value()) {
            segment.header().ack = true;
            segment.header().ackno = _receiver.ackno().value();
        }
        segment.header().win = min(static_cast<size_t>(numeric_limits<uint16_t>::max()), _receiver.window_size());
        _segments_out.emplace(std::move(segment));
    }
}

void TCPConnection::set_shutdown(const bool is_clean) {
    _is_active = false;
    if (!is_clean) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
    }
}

// 生成带有 RST 标志位的空 segment
void TCPConnection::send_rst_segment() {
    _sender.send_empty_segment(false, false, true);
    segment_assemble_send();
}