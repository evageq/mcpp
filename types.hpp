#ifndef __TYPES_H__
#define __TYPES_H__

#include <cstdint>

class ClientId
{
private:
    std::uint64_t id_ = 0;

public:
    ClientId() = default;
    explicit ClientId(std::uint64_t value) : id_(value) {}

    std::uint64_t
    GetId() const
    {
        return id_;
    }

    bool
    operator ==(const ClientId &other) const
    {
        return id_ == other.id_;
    }

    bool
    operator <(const ClientId &other) const
    {
        return id_ < other.id_;
    }
};

#endif // __TYPES_H__
