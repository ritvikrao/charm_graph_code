#pragma once
namespace PUP {
struct er {
  template <typename T> er &operator|(T &) { return *this; }
};
}
