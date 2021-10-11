#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : s(""), scapacity(capacity), bytesWritten(0), bytesRead(0), writeEnded{0} {}

size_t ByteStream::write(const string &data) {
    size_t rc = remaining_capacity();
    if (data.size() < rc) {
        s = s + data;
        bytesWritten += data.size();
        return data.size();
    } else {
        s = s + data.substr(0, rc);
        bytesWritten += rc;
        return rc;
    }
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    string res;
    if (len <= s.size())
        res = s.substr(0, len);
    else
        res = s;
    return res;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    if (len == s.size())
        s.clear();
    else
        s = s.substr(len);
    bytesRead += len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string res = peek_output(len);
    pop_output(res.size());
    return res;
}

void ByteStream::end_input() { writeEnded = true; }

bool ByteStream::input_ended() const { return writeEnded; }

size_t ByteStream::buffer_size() const { return s.size(); }

bool ByteStream::buffer_empty() const { return !s.size(); }

bool ByteStream::eof() const { return writeEnded && !s.size(); }

size_t ByteStream::bytes_written() const { return bytesWritten; }

size_t ByteStream::bytes_read() const { return bytesRead; }

size_t ByteStream::remaining_capacity() const {
    size_t cs = s.size();
    return scapacity - cs;
}
