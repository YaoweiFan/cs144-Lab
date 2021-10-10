#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _capacity(capacity), _index(0), _aux_vec(), _eof(false) {}

void StreamReassembler::push_to_aux(const string &data, const size_t index, const bool eof) {
    // 若出现 eof，则标记 _eof
    if (!_eof && eof) _eof = !_eof;
    // 如果字符串为空，则没有必要再压入辅助空间
    if(data == "") return;
    // 创建该 substring 的 substr 结构体，方便下面某些情况下直接插入
    struct substr elem;
    elem.index_start = index;
    elem.s = data;  // 这里是深拷贝吗？应该是深拷贝

    // 遍历以寻找最佳插入点
    size_t i = 0;
    for (; i < _aux_vec.size(); i++) {
        if (index + data.size() < _aux_vec[i].index_start) {
            // 整个在当前 substr 之前（无任何重叠）
            _aux_vec.insert(_aux_vec.begin() + i, elem);
            break;
        } else if (index > _aux_vec[i].index_start + _aux_vec[i].s.size())  
            // 整个在当前 substr 之后（无任何重叠）
            continue;
        else {  
            // 与当前 substr 有重叠
            if (_aux_vec[i].index_start < index) {
                if (_aux_vec[i].index_start + _aux_vec[i].s.size() < index + data.size())
                    _aux_vec[i].s = _aux_vec[i].s + data.substr(_aux_vec[i].index_start + _aux_vec[i].s.size() - index);
            } else {
                if (_aux_vec[i].index_start + _aux_vec[i].s.size() > index + data.size())
                    _aux_vec[i].s = data + _aux_vec[i].s.substr(index + data.size() - _aux_vec[i].index_start);
                else
                    _aux_vec[i].s = data;
            }
            _aux_vec[i].index_start = _aux_vec[i].index_start < index ? _aux_vec[i].index_start : index;
            break;
        }
    }
    // 如果遍历后仍然找不到插入点，就直接放入 _aux_vec 的末尾
    if (i == _aux_vec.size())
        _aux_vec.push_back(elem);

    // 将 index 有重合的 _aux_vec 进行整合
    struct substr &item = _aux_vec[i];
    std::vector<substr>::iterator iter = _aux_vec.begin() + i + 1;
    while (iter != _aux_vec.end()) {
        if (item.index_start + item.s.size() >= iter->index_start) {
            if (item.index_start + item.s.size() < iter->index_start + iter->s.size())
                item.s = item.s + iter->s.substr(item.index_start + item.s.size() - iter->index_start);
            iter = _aux_vec.erase(iter);  // 返回指向下一个元素的迭代器
        } else
            break;
    }

    // 删去超出 _capacity 的那部分字符串
    size_t total_bytes_in_auxspace = unassembled_bytes();
    if(total_bytes_in_auxspace > _capacity){
        // _eof 标志位失效
        if (eof) _eof = !_eof;
        iter = _aux_vec.end() - 1;
        size_t bytes_need_delete = total_bytes_in_auxspace - _capacity;
        while (1){
            size_t curr_substring_size = iter->s.size();
            if (bytes_need_delete < curr_substring_size){
                iter->s = iter->s.substr(0, curr_substring_size-bytes_need_delete);
                break;
            } else {
                _aux_vec.erase(iter);
                iter--;
                bytes_need_delete -= curr_substring_size;
            }   
        }  
    }
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // 对 push 进来的 substring 进行 index 检查，如果 index 小于 _index，则需要作一些修改
    string data_ = data;
    size_t index_ = index;
    if(index < _index && index + data.size() > _index){
        data_ = data.substr(_index-index);
        index_ = _index;
    }
    if (index + data.size() <= _index){
        data_ = "";
        index_ = 0;
    }

    // 将 substring 压入辅助空间
    push_to_aux(data_, index_, eof);

    // reassemble
    std::vector<substr>::iterator iter = _aux_vec.begin();
    while (iter != _aux_vec.end()) {
        // 若 _output 需要的下一个字节能够被提供，则对这一项进行 reassemble
        if (_index >= iter->index_start && _index < iter->index_start + iter->s.size()) {
            size_t written_size = _output.write(iter->s.substr(_index - iter->index_start));
            // 可能写不下该项的所有字节
            if (written_size < iter->s.substr(_index - iter->index_start).size())
                iter->s = iter->s.substr(_index - iter->index_start + written_size);
            else
                iter = _aux_vec.erase(iter);
            _index += written_size;
            break;
        } else if (_index >= iter->index_start + iter->s.size())  
            // index 在该项之后，则丢弃这一项，查看下一项是否符合
            iter = _aux_vec.erase(iter);
        else break; // index 在两项之间（或者在第一项之前）
    }

    // 如果辅助空间已经 empty 且 _eof 标志位已经被置上，就结束 reassemble
    if (_aux_vec.size() == 0 && _eof) _output.end_input();
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t total = 0;
    for (size_t i = 0; i < _aux_vec.size(); i++)
        total += _aux_vec[i].s.size();
    return total;
}

bool StreamReassembler::empty() const {
    if (_aux_vec.size() == 0) return true; else return false;
}
