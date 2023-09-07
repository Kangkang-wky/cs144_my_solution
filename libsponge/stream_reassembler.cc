#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity)
    , _capacity(capacity)
    , _unassembled_bytes()
    , _unassembled_cnt(0)
    , _unassembled_index(0)
    , _unaccepted_index(0)
    , _eof_index(numeric_limits<size_t>::max()) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // 如果该字段包含 eof, 则将 _eof_index 置到正确的位置上
    if (eof) {
        _eof_index = index + data.size();
    }

    // 如果当前 eof 已经被读入到 output_buffer 中退出 push
    if (_unassembled_index >= _eof_index) {
        _output.end_input();
        return;
    }

    // data 整体在 _unassembled_index 之前
    // data 整体在 _unaccepted_index 之后
    _unaccepted_index = _output.bytes_read() + _capacity;
    if (index + data.size() < _unassembled_index || index > _unaccepted_index) {
        return;
    }

    // index > _unassembled_index
    if (index > _unassembled_index) {
        merge_string(const_cast<std::string &>(data), index);
    } else {
        // index <= _unassembled_index 的做左右两次切割
        // 左侧切割 _unassembled 部分, 右侧切割 _unaccept 部分以及做好容量控制
        size_t bound_index = min(index + data.size(), _unaccepted_index);
        string write_data =
            data.substr(_unassembled_index - index,
                        min(bound_index - _unassembled_index, _capacity - _output.buffer_size() - unassembled_bytes()));
        size_t written_size = _output.write(write_data);
        _unassembled_index += written_size;

        // output write 之后检查 map 中是否有可以写入的部分了
        // 非 unordered_map 默认按照键的大小升序排列检查 map_index <= _unassembled_index 即可
        while (!_unassembled_bytes.empty()) {
            auto &[map_index, map_str] = *_unassembled_bytes.begin();
            if (map_index <= _unassembled_index) {
                if (map_index + map_str.size() > _unassembled_index) {
                    written_size = _output.write(map_str.substr(_unassembled_index - map_index));
                    _unassembled_index += written_size;
                }
                _unassembled_cnt -= (*_unassembled_bytes.begin()).second.size();
                _unassembled_bytes.erase(_unassembled_bytes.begin());
            } else {
                break;
            }
        }
    }
    // eof 字段被组装, 结束
    if (_unassembled_index >= _eof_index) {
        _output.end_input();
    }
}

void StreamReassembler::merge_string(string &data, uint64_t index) {
    // 左闭右开区间
    size_t st = index, ed = index + data.size();

    auto iter = _unassembled_bytes.begin();

    while (iter != _unassembled_bytes.end()) {
        // 跳出循环
        if (index + data.size() <= (*iter).first) {
            break;
        }
        const string &sub_str = (*iter).second;
        size_t map_st = (*iter).first, map_ed = map_st + sub_str.size();
        // 需要进行覆盖或者重叠操作
        if (st < map_ed && map_st < ed) {
            // 插入的部分 覆盖 map 的部分
            if (st <= map_st && ed >= map_ed) {
                _unassembled_cnt -= (*iter).second.size();
                iter = _unassembled_bytes.erase(iter);
            }
            // map的部分 覆盖 插入的部分
            else if (map_st <= st && map_ed >= ed) {
                data.clear();
                ++iter;
            }
            // 插入部分 与 map 中有交叉
            else {
                // data 在 map 元素之前
                if (st <= map_st) {
                    data += sub_str.substr(ed - map_st, map_ed - ed);
                }
                // data 在 map 元素之后
                else {
                    index = map_st;
                    data.insert(0, sub_str.substr(0, st - map_st));
                }
                _unassembled_cnt -= (*iter).second.size();
                iter = _unassembled_bytes.erase(iter);
            }
        } else {
            ++iter;
        }
    }

    if (!data.empty()) {
        // 实际插入需要进行容量检查, 两次容量检查确定读入多少字符
        _unaccepted_index = _output.bytes_read() + _capacity;
        size_t bound_index = min(index + data.size(), _unaccepted_index);
        size_t insert_size = min(bound_index - index, _capacity - _output.buffer_size() - unassembled_bytes());
        _unassembled_cnt += insert_size;
        _unassembled_bytes.insert(make_pair(index, data.substr(0, insert_size)));
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_cnt; }

bool StreamReassembler::empty() const { return _unassembled_bytes.empty(); }