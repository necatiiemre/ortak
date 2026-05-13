#pragma once
#include <stdexcept>
#include <string>

class DirectoryException : public std::runtime_error {
public:
    (int code, const std::string& msg)
        : std::runtime_error(msg), errorCode(code) {}

    int getCode() const { return errorCode; }

private:
    int errorCode;
};
