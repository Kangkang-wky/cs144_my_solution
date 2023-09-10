#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    // DUMMY_CODE(n, isn);
    // seqno = (isn + absolute_seqno) mod 32
    uint32_t absolute_seqno = isn.raw_value() + static_cast<uint32_t>(n);
    return WrappingInt32{std::move(absolute_seqno)};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // DUMMY_CODE(n, isn, checkpoint);
    // n ==同余 (absolute_seqno + isn) mod 2^32
    // abolute_seqno ==同余 (n - isn) mod 2^32
    // absolute = (n - isn) + 2^32 * rank 离 checkpoint 近的那一个
    const uint64_t num = n.raw_value() - isn.raw_value();
    if (checkpoint > num) {
        // (absolute - (n - isn) + 一半) / 2 ^ 32  求离它最近的一个
        // 也可以 检查 (n - isn) 的 低 32 位来判断最近， 检查 checkpoint 的高32位来判断 rank
        uint64_t rank = ((checkpoint - num) + (1l << 31)) / (1l << 32);
        uint64_t real_num = num + rank * (1l << 32);
        return real_num;

    }
    // checkpoint 本身在 32 位无符号数里面
    else {
        return num;
    }
}
