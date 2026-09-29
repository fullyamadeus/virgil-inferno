#pragma once

#include <QString>
#include <stdexcept>

namespace virgil {
class Error : public std::runtime_error {
public:
    explicit Error(const QString& message) : std::runtime_error(message.toStdString()) {}
};
} // namespace virgil
