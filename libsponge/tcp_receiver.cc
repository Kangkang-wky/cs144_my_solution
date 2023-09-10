#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

// TCP_recv 的三种状态转换
// LISTEN  SYN 尚未到达
// SYN_RECV SYN 已经到达
// FIN_RECV  FIN 到达, 通过字节流是否关闭来判断

// 处理接收到的 TCP segment
void TCPReceiver::segment_received(const TCPSegment &seg) {
    // DUMMY_CODE(seg);
    const TCPHeader &header = seg.header();
    const bool syn = header.syn;  // 标记 TCP报文头 syn
    const bool fin = header.fin;  // 标记 TCP报文头 fin eof

    // LISTEN 状态 SYN 标志位为 false
    if (!_set_syn) {
        // 当前 segment syn 为 false
        if (!syn) {
            return;
        }
        // 当前 segment syn 为 true 收到 SYN 转为 SYN_RECV 状态
        _set_syn = true;
        _isn = header.seqno;
    }

    // 用 unassembled_index 来作为 checkpoint
    auto checkpoint = _reassembler.stream_out().bytes_written() + 1;
    // unwrap 后得到 64bit abs_seqno
    uint64_t abs_seqno = unwrap(header.seqno, _isn, checkpoint);

    // 从 abs_seqno 得到有效字节流序号, 非 syn segment 去掉 syn 的占位
    uint64_t stream_index = abs_seqno + (syn ? 0 : -1);
    _reassembler.push_substring(seg.payload().copy(), stream_index, fin);

    // FIN_RECV 状态
    if (fin) {
        _set_fin = true;
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    // 如果是 Listen 状态
    if (!_set_syn) {
        return nullopt;
    }
    uint64_t unassembled_index = _reassembler.stream_out().bytes_written() + 1;
    // 如果是 FIN_RECV 状态 + SYN 标志位占了一个 + FIN 标志位占了一个
    if (_reassembler.stream_out().input_ended()) {
        return wrap(unassembled_index + 1, _isn);
    }
    // 如果是 SYN_RECV 状态 + SYN 标志位占了一个
    else {
        return wrap(unassembled_index, _isn);
    }
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
