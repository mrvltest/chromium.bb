/*
 * Copyright (C) 2013 Bloomberg Finance L.P.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS," WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <blpwtk2_string.h>

#include <base/logging.h>  // for DCHECK
#include <base/utf_string_conversions.h>
#include <third_party/WebKit/Source/Platform/chromium/public/WebCString.h>
#include <third_party/WebKit/Source/Platform/chromium/public/WebString.h>

namespace blpwtk2 {

String::Impl String::make(const char* str, int length)
{
    DCHECK(0 <= length);
    if (0 == length) return 0;
    DCHECK(str);

    int* lenPtr = (int*)malloc(sizeof(int) + length + 1);
    *lenPtr = length;

    char* ret = {reinterpret_cast<char*>(lenPtr + 1)};
    memcpy(ret, str, length);
    ret[length] = 0;
    return ret;
}

String::Impl String::make(const wchar_t* str, int length)
{
    // TODO: There is an extra copy going from:
    // TODO:     wchar_t* -> std::string -> blpwtk2::String
    // TODO: We should optimize this to do:
    // TODO:     wchar_t* -> blpwtk2::String
    std::string tmp;
    WideToUTF8(str, length, &tmp);
    return make(tmp.data(), tmp.length());
}

String::Impl String::make(Impl impl)
{
    DCHECK(impl);

    const char* str = impl;
    int length = *(reinterpret_cast<int*>(impl) - 1);
    DCHECK(0 < length);

    int* lenPtr = (int*)malloc(sizeof(int) + length + 1);
    *lenPtr = length;

    char* ret = {reinterpret_cast<char*>(lenPtr + 1)};
    memcpy(ret, str, length);
    ret[length] = 0;
    return ret;
}

void String::unmake(Impl impl)
{
    DCHECK(impl);
    int* realPtr = reinterpret_cast<int*>(impl) - 1;
    free(realPtr);
}

int String::length(Impl impl)
{
    DCHECK(impl);
    return *(reinterpret_cast<int*>(impl) - 1);
}

String fromWebString(const WebKit::WebString& other)
{
    WebKit::WebCString cstr = other.utf8();
    // TODO: see if we can "steal" this data from WebCString instead of
    // TODO: copying it
    return String(cstr.data(), cstr.length());
}

}  // close namespace blpwtk2

