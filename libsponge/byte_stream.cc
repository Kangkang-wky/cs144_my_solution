#include "byte_stream.hh"

#include <cstddef>
#include <iterator>
#include <unistd.h>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : _byte_stream()
    , _capacity(capacity)
    , _byte_written_size(0)
    , _byte_read_size(0)
    , _input_end(false)
    , _error(false) {}

// 输入端写入到 buffer 中, 考虑容量是否满足写入的大小
size_t ByteStream::write(const string &data) {
    if (input_ended())
        return 0;
    size_t written_size = 0;
    written_size = min(data.size(), _capacity - _byte_stream.size());
    _byte_stream.insert(_byte_stream.end(), data.begin(), data.begin() + written_size);
    _byte_written_size += written_size;
    return written_size;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t read_size = min(len, _byte_stream.size());
    string read_output(_byte_stream.begin(), _byte_stream.begin() + read_size);
    return read_output;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t read_size = min(len, _byte_stream.size());
    _byte_stream.erase(_byte_stream.begin(), _byte_stream.begin() + read_size);
    _byte_read_size += read_size;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    std::string read_output = peek_output(len);
    pop_output(read_output.size());
    return read_output;
}

void ByteStream::end_input() { _input_end = true; }

bool ByteStream::input_ended() const { return _input_end; }

size_t ByteStream::buffer_size() const { return _byte_stream.size(); }

bool ByteStream::buffer_empty() const { return _byte_stream.empty(); }

bool ByteStream::eof() const { return _input_end && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _byte_written_size; }

size_t ByteStream::bytes_read() const { return _byte_read_size; }

size_t ByteStream::remaining_capacity() const { return _capacity - _byte_stream.size(); }
