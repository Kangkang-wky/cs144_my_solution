#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>

// 构建一个 Timer 类, 用作定时器

class Timer {
  private:
    uint64_t _init_RTO = 0;      // 初始RTO
    uint64_t _cur_RTO = 0;       // 指数避退的RTO
    uint64_t _elapsed_time = 0;  // 定时器开始到现在的时间, 通过 tick 来更新
    bool _running = false;       // 记录定时器是否开始

  public:
    Timer() {}
    Timer(const uint64_t RTO) : _init_RTO(RTO), _cur_RTO(RTO) {}
    // 计时器 start
    void start() {
        _elapsed_time = 0;
        _running = true;
    }

    // 定时器停止
    void stop() { _running = false; }

    // 定时器重置
    void reset() { _cur_RTO = _init_RTO; }

    // 指数避退
    void reset_double() { _cur_RTO *= 2; }

    // 定时器 tick
    void tick(const size_t ms_since_last_tick) {
        if (check_running()) {
            _elapsed_time += ms_since_last_tick;
        }
    }

    // check expired
    bool check_expired() const { return _running && (_elapsed_time >= _cur_RTO); }

    // check running
    bool check_running() const { return _running; }
};

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    // 定义一些自己写的成员变量
    uint64_t _recv_ackno{0};
    // 设置标志位记录是否为syn 或者 fin 状态
    bool _set_syn = false;
    bool _set_fin = false;
    // 定时器设置
    Timer _timer;
    // 连续重传次数
    uint64_t _consecutive_retransmissions_cnt{0};
    // 窗口大小
    // _window_size 初始化为 1，否则TCP 刚开始就丢包的话 _window_size = 0 只会认为接收方窗口为0, 不会做指数退避 RTO*2
    uint64_t _window_size{1};
    // 已经发送出去但是还未被 ack 确认的字节数, 即 _segments_unackno 的字节数
    uint64_t _bytes_in_flight{0};
    // 符合上述描述的 TCPSegment 字段, 保存那些没有被ack 确认的 TCPSegment
    // _segments_out 保存的是发送的 TCPsegment
    std::queue<TCPSegment> _segments_unackno{};

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}

    // 组装好 TCPsegment push 到 segment_out 中
    bool push_segment(TCPSegment &segment, size_t &length);
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
