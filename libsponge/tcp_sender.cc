#include "tcp_config.hh"
#include "tcp_sender.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _timer(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

// 填充发送窗口
void TCPSender::fill_window() {
    size_t fill_window_size = 0;
    // 接收窗口为0, 发送方按照接收方 window_size 为 1 操作, 主要是为了 keep alive
    // 为了
    if (_window_size == 0) {
        while (true) {
            TCPSegment segment;
            // bytes_in_flight() < _window_size
            if (bytes_in_flight() == 0) {
                fill_window_size = 1;
            }
            // bytes_in_flight() >= _window_size
            else {
                return;
            }
            if (!push_segment(segment, fill_window_size)) {
                break;
            }
        }
    } else {
        while (true) {
            TCPSegment segment;
            if (_window_size > bytes_in_flight()) {
                fill_window_size = _window_size - bytes_in_flight();
            }
            // _window_size <= bytes_in_flight() 不需要填充窗口, 被 unack 的 TCP segmengt 占满了
            else {
                fill_window_size = 0;
            }
            if (!push_segment(segment, fill_window_size)) {
                break;
            }
        }
    }
}

bool TCPSender::push_segment(TCPSegment &segment, size_t &length) {
    if (!_set_syn) {
        segment.header().syn = true;
        _set_syn = true;
    }

    if (length < segment.header().syn) {
        return false;
    }

    // payload 不包括 SYN 和 FIN, 但是 window_size 包括 SYN 和 FIN
    size_t payload_size = min(TCPConfig::MAX_PAYLOAD_SIZE, min(length - segment.header().syn, _stream.buffer_size()));

    auto payload = _stream.read(payload_size);
    segment.payload() = Buffer(std::move(payload));

    // 设置fin 要求之前没有置位 FIN, 且读到 eof, 且 发送窗口还有空间
    if (!_set_fin && _stream.eof() && segment.length_in_sequence_space() < length) {
        segment.header().fin = true;
        _set_fin = true;
    }

    // 空数据报就不发送了
    auto segment_lenth = segment.length_in_sequence_space();
    if (segment_lenth <= 0) {
        return false;
    }

    // Tcp segmengt 头加上 seqno
    // 将 TCP segment 放到 _segments_out 队列中发送
    // 发送即若定时器启动定时器
    segment.header().seqno = next_seqno();
    _segments_out.push(segment);
    if (!_timer.check_running())
        _timer.start();

    // unack队列保留未确认的 TCP segment
    // 更新下一个序列号_next_seqno和发出但未 ACK 的字节数即_segmengts_unack队列
    _segments_unackno.emplace(std::move(segment));
    _next_seqno += segment_lenth;
    _bytes_in_flight += segment_lenth;
    return true;
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 使用 _recv_ackno 作为 checkpoint
    // _recv_ackno 表示下一个期待发送方发送的 seqno
    uint64_t abs_ackno = unwrap(ackno, _isn, _recv_ackno);

    // 检查 abs_ackno 是否可靠
    if (abs_ackno > _next_seqno || abs_ackno < _recv_ackno) {
        return;
    }
    // _recv_ackno 确认到 abs_ackno
    _recv_ackno = abs_ackno;

    bool ack_update_flag = false;

    // 检查 ack 是否确认了 outstanding 中新的 TCPsegment
    while (!_segments_unackno.empty()) {
        auto segment = _segments_unackno.front();
        uint64_t segment_abs_seqno = unwrap(segment.header().seqno, _isn, _next_seqno);
        uint64_t segment_lenth = segment.length_in_sequence_space();

        if (segment_abs_seqno + segment_lenth <= _recv_ackno) {
            ack_update_flag = true;
            _segments_unackno.pop();
            _bytes_in_flight -= segment_lenth;
        } else {
            break;
        }
    }

    // ack 如果更新了, 定时器以及选择重传数进行重置
    if (ack_update_flag) {
        _consecutive_retransmissions_cnt = 0;
        _timer.reset();
        _timer.start();
    }
    // ack 确认后如果 unack 队列为空让定时器暂停
    if (_segments_unackno.empty()) {
        _timer.stop();
    }
    // 重新填充发送窗口
    _window_size = window_size;
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // 检查定时器是否启动
    if (_timer.check_running()) {
        _timer.tick(ms_since_last_tick);
    }
    // 定时器已经启动, 且定时器超时
    // 防御性编程, 检查是否为空
    if (_timer.check_expired() && !_segments_unackno.empty()) {
        // 重传最早未被确认的 TCP segment FIFO
        _segments_out.push(_segments_unackno.front());

        // 接收窗口的反馈为 0 时, 说明接收方没有能力接收, 但并不代表网络拥塞
        // 接收窗口大于 0, 定时器超时, 网络拥塞重传, 指数避退
        if (_window_size > 0) {
            _consecutive_retransmissions_cnt++;
            _timer.reset_double();
        }
        _timer.start();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions_cnt; }

// 该函数发送空的数据包, 仅用于 ACK 确认完成
void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = next_seqno();
    _segments_out.emplace(std::move(segment));
}

void TCPSender::send_empty_segment(bool SYN, bool FIN, bool RST) {
    TCPSegment segment;
    segment.header().seqno = next_seqno();
    if (SYN) {
        _set_syn = true;
    }
    if (FIN) {
        _set_fin = true;
    }
    segment.header().syn = SYN;
    segment.header().fin = FIN;
    segment.header().rst = RST;
    _next_seqno += segment.length_in_sequence_space();
    _segments_out.emplace(std::move(segment));
}