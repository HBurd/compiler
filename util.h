#pragma once

#include <cassert>

template <typename T, uint32_t max_size>
struct Array
{
    uint32_t size = 0;
    T data[max_size];
    T& push(T element)
    {
        assert(size < max_size);
        data[size++] = element;
        
        return back();
    }

    T pop()
    {
        assert(size);
        return data[--size];
    }

    T& operator[](uint32_t idx)
    {
        return data[idx];
    }

    const T& operator[](uint32_t idx) const
    {
        return data[idx];
    }

    T& back()
    {
        assert(size);
        return data[size - 1];
    }
};
