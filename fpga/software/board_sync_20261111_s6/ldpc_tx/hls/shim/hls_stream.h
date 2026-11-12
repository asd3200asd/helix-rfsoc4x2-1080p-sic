#pragma once
#include <deque>
#include <cstddef>
namespace hls { template <class T> class stream { std::deque<T> q; public: void write(const T& t) { q.push_back(t); } T read() { T t = q.front(); q.pop_front(); return t; } bool empty() const { return q.empty(); } size_t size() const { return q.size(); } }; }
