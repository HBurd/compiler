#pragma once

#include <cassert>
#include <stdint.h>

template <typename T>
struct Array
{
    uint32_t length = 0;
    uint32_t max_length = 0;
    T *data;

    T* push(T element)
    {
        assert(length < max_length);
        data[length++] = element;
        
        return back();
    }

    T pop()
    {
        assert(length);
        return data[--length];
    }

    T& operator[](uint32_t idx)
    {
        return data[idx];
    }

    const T& operator[](uint32_t idx) const
    {
        return data[idx];
    }

    T* back()
    {
        assert(length);
        return &data[length - 1];
    }

    T* begin()
    {
        return data;
    }

    T* end()
    {
        return data + length;
    }

    const T* begin() const
    {
        return data;
    }

    const T* end() const
    {
        return data + length;
    }
};

struct SubString
{
    const char* start = nullptr;
    uint32_t len = 0;

    void print() const;
    bool operator==(const SubString& rhs);
};

bool operator==(const SubString& lhs, const char* rhs);
bool operator==(const char* lhs, const SubString& rhs);
