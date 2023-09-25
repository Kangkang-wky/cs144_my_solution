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

void TCPSender::fill_window() {
    size_t fill_window_size = 0;
    if (_window_size == 0) {
        if (bytes_in_flight() == 0) {
            TCPSegment segment;
            size_t length = 1;
            push_segment(segment, length);
        } else {
            return;
        }
    } else {
        while (true) {
            TCPSegment segment;
            if (_window_size > bytes_in_flight()) {
                fill_window_size = _window_size - bytes_in_flight();
            } else {
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

    // 读到了 eof
    // 如果发送当前的数据后，且仍有空间携带 FIN，那么直接带上 FIN。
    if (!_set_fin && _stream.eof() && segment.length_in_sequence_space() < length) {
        segment.header().fin = true;
        _set_fin = true;
    }

    // 空数据报就不发送了
    auto segment_lenth = segment.length_in_sequence_space();
    if (segment_lenth <= 0) {
        return false;
    }

    // Tcp segmengt seqno 加上
    segment.header().seqno = next_seqno();
    _segments_out.push(segment);

    // 如果定时器关闭，则启动定时器
    if (!_timer.check_running())
        _timer.start();

    // 保存备份，重发时可能会用
    _segments_unackno.emplace(std::move(segment));

    // 更新序列号和发出但未 ACK 的字节数
    _next_seqno += segment_lenth;
    _bytes_in_flight += segment_lenth;
    return true;
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // DUMMY_CODE(ackno, window_size);
    // 使用 _next_seqno 作为 checkpoint
    _recv_ackno = unwrap(ackno, _isn, _recv_ackno);

    // 检查 abs_ackno 是否可靠
    if (_recv_ackno > _next_seqno) {
        return;
    }

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

    // ack 如果更新了, 定时器以及相关变量重置
    if (ack_update_flag) {
        _consecutive_retransmissions_cnt = 0;
        _timer.reset();
        _timer.start();
    }

    if (_segments_unackno.empty()) {
        _timer.stop();
    }

    _window_size = window_size;
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // 检查计时器是否启动, 加上 tick 时间
    if (_timer.check_running()) {
        _timer.tick(ms_since_last_tick);
    }
    // 计时器已经启动, 且计时器超时
    // 防御性编程, 检查是否为空
    if (_timer.check_expired() && !_segments_unackno.empty()) {
        // 重传最早未被确认的 TCP segment
        _segments_out.push(_segments_unackno.front());

        // 发送串口大小不为0, 选择重传
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
