#pragma once

#include <QString>
#include <QtGlobal>

namespace lighttrack
{
class ClipId
{
public:
    constexpr ClipId() noexcept = default;
    explicit constexpr ClipId(quint64 value) noexcept :
        value_(value)
    {
    }

    constexpr bool IsValid() const noexcept
    {
        return value_ != 0;
    }

    constexpr quint64 Value() const noexcept
    {
        return value_;
    }

    friend constexpr bool operator==(ClipId left, ClipId right) noexcept
    {
        return left.value_ == right.value_;
    }

    friend constexpr bool operator!=(ClipId left, ClipId right) noexcept
    {
        return !(left == right);
    }

    friend constexpr bool operator<(ClipId left, ClipId right) noexcept
    {
        return left.value_ < right.value_;
    }

private:
    quint64 value_ = 0;
};

struct TimelineLane
{
    QString id;
    QString name;
    int level = 0;
    bool highlighted = false;
    bool disabled = false;
};

struct TimelineClip
{
    ClipId id;
    QString effect_id;
    int lane_index = -1;
    qint64 start_ms = 0;
    qint64 end_ms = 0;
};
}
