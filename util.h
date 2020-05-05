#pragma once

#include <cassert>
#include <stdint.h>

template <typename T, uint32_t MAX_LEN>
struct StaticArray
{
    uint32_t length = 0;
    T data[MAX_LEN];

    static const uint32_t max_length = MAX_LEN;

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

    const T* operator[](uint32_t idx) const
    {
        return &data[idx];
    }

    T* back()
    {
        assert(length);
        return &data[length - 1];
    }
};

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

    const T* operator[](uint32_t idx) const
    {
        return &data[idx];
    }

    T* back()
    {
        assert(length);
        return &data[length - 1];
    }
};
