/*
    Boost Software License - Version 1.0 - August 17, 2003

    Permission is hereby granted, free of charge, to any person or organization
    obtaining a copy of the software and accompanying documentation covered by
    this license (the "Software") to use, reproduce, display, distribute,
    execute, and transmit the Software, and to prepare derivative works of the
    Software, and to permit third-parties to whom the Software is furnished to
    do so, all subject to the following:

    The copyright notices in the Software and this entire statement, including
    the above license grant, this restriction and the following disclaimer,
    must be included in all copies of the Software, in whole or in part, and
    all derivative works of the Software, unless such copies or derivative
    works are solely in the form of machine-executable object code generated by
    a source language processor.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
    SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
    FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
    ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#ifndef REFLECTOR_HAVE_SERIALIZER
#define REFLECTOR_HAVE_SERIALIZER

#include "base.hpp"
#include "bufstring.hpp"

#ifndef REFLECTOR_AVOID_STL
#include <string>
#include <vector>
#endif

namespace serialization {
using reflection::BufString_t;

enum {
    TAG_NO_TYPE         = 0x00,

    // single-value
    TAG_VOID            = 0x01,     // void (no value)
    TAG_BOOL            = 0x02,     // bool (1 byte)
    TAG_CHAR            = 0x03,     // [un]signed char (1 byte)
    TAG_SMVINT          = 0x04,     // sign+magnitude variable-length int
    TAG_REAL32          = 0x05,     // float (4 bytes)
    TAG_REAL64          = 0x06,     // float (8 bytes)
    // array
    TAG_UTF8            = 0x08,     // UTF-8 string (SmvInt length IN BYTES + utf8chars...)
    TAG_TYPED_ARRAY     = 0x09,     // typed array (1 byte type tag + SmvInt length + items...)
    TAG_FIXED_ARRAY     = 0x0A,     // fixed array (1 byte elemSize + SmvInt length + values...)
    // complex types
    TAG_CLASS           = 0x0C,
    TAG_CLASS_SCHEMA    = 0x0D,
};

typedef uint8_t Tag_t;
static_assert(sizeof(Tag_t) == 1, "uint8_t must be 8 bits");

template <typename T>
class Serializer {
};

template <class IErrorHandler>
bool checkTag(IErrorHandler* err, IReader* reader, Tag_t expected) {
    Tag_t tag;

    if (!reader->read(err, &tag, sizeof(tag)))
        return false;

    if (tag != expected)
        return err->errorf("IncorrectType", "Unexpected tag 0x%02X, expected 0x%02X.", tag, expected),
                false;

    return true;
}

template <class IErrorHandler>
bool writeTag(IErrorHandler* err, IWriter* writer, Tag_t tag) {
    return writer->write(err, &tag, sizeof(tag));
}

template <>
class Serializer<bool> {
public:
    enum { TAG = TAG_BOOL };

    static bool serialize(IErrorHandler* err, IWriter* writer, const bool& value) {
        uint8_t normalizedValue = value ? 0x01 : 0x00;
        return writer->write(err, &normalizedValue, 1);
    }

    static bool deserialize(IErrorHandler* err, IReader* reader, bool& value_out) {
        uint8_t value;

        if (!reader->read(err, &value, 1))
            return false;

        value_out = (value != 0);
        return true;
    }
};

template <typename T>
class CharSerializer {
    static_assert(sizeof(T) == 1, "CharSerializer expects a 1-byte type.");
public:
    enum { TAG = TAG_CHAR };

    static bool serialize(IErrorHandler* err, IWriter* writer, const T& value) {
        return writer->write(err, &value, 1);
    }

    static bool deserialize(IErrorHandler* err, IReader* reader, T& value_out) {
        return reader->read(err, &value_out, 1);
    }
};

template <typename T, Tag_t tag>
class FloatSerializer {
public:
    enum { TAG = tag };

    static bool serialize(IErrorHandler* err, IWriter* writer, const T& value) {
        return writer->write(err, &value, 1);
    }

    static bool deserialize(IErrorHandler* err, IReader* reader, T& value_out) {
        return reader->read(err, &value_out, 1);
    }
};

template <typename T>
class SmvIntSerializer {
public:
    enum { TAG = TAG_SMVINT };

    static bool serializeValue(IErrorHandler* err, IWriter* writer, const T& value) {
        uint64_t sign, magnitude, signMask;

        if (value >= 0) {
            magnitude = value;
            sign = 0;
        }
        else {
            // FIXME: check overflow
            magnitude = 1 + ~value;
            sign = 1;
        }

        signMask = 0x40;

        // glue magnitude onto the value so that it will always be
        // the most significant encoded bit
        while ((magnitude & (signMask - 1)) != magnitude)
            // FIXME: check overflow
            signMask = signMask << 7;

        magnitude |= sign * signMask;

        // from now on, signMask is actually signMask|magnitudeMask
        signMask |= (signMask - 1);

        uint8_t byte;

        while (signMask != 0) {
            byte = magnitude & 0x7f;
            magnitude = (magnitude >> 7);
            signMask = (signMask >> 7);

            if (signMask != 0)
                byte |= 0x80;

            if (!writer->write(err, &byte, 1))
                return false;
        }

        return true;
    }

    static bool deserializeValue(IErrorHandler* err, IReader* reader, T& value_out) {
        uint64_t magnitude = 0;
        uint8_t byte;

        unsigned int shift = 0;

        for (;;) {
            // FIXME: check overflow + max length
            if (!reader->read(err, &byte, 1))
                return false;

            if (byte & 0x80) {
                magnitude |= (byte & 0x7f) << shift;
                shift += 7;
            }
            else {
                magnitude |= (byte & 0x3f) << shift;

                if (byte & 0x40) {
                    // negative
                    // FIXME: check overflow
                    value_out = 1 + (T) ~magnitude;
                }
                else
                    value_out = (T) magnitude;

                return true;
            }
        }
    }

    static bool serialize(IErrorHandler* err, IWriter* writer, const T& value) {
        return serializeValue(err, writer, value);
    }

    static bool deserialize(IErrorHandler* err, IReader* reader, T& value_out) {
        return deserializeValue(err, reader, value_out);
    }
};

template <> class Serializer<char> :                public CharSerializer<char> {};
template <> class Serializer<unsigned char> :       public CharSerializer<unsigned char> {};

template <> class Serializer<short> :               public SmvIntSerializer<short> {};
template <> class Serializer<int> :                 public SmvIntSerializer<int> {};
template <> class Serializer<long> :                public SmvIntSerializer<long> {};
template <> class Serializer<long long> :           public SmvIntSerializer<long long> {};

template <> class Serializer<unsigned short> :      public SmvIntSerializer<unsigned short> {};
template <> class Serializer<unsigned int> :        public SmvIntSerializer<unsigned int> {};
template <> class Serializer<unsigned long> :       public SmvIntSerializer<unsigned long> {};
template <> class Serializer<unsigned long long> :  public SmvIntSerializer<unsigned long long> {};

template <> class Serializer<float> :               public FloatSerializer<float, TAG_REAL32> {};
template <> class Serializer<double> :              public FloatSerializer<double, TAG_REAL64> {};

template <>
class Serializer<BufString_t> {
public:
    enum { TAG = TAG_UTF8 };

    static bool serialize(IErrorHandler* err, IWriter* writer, const char* value) {
        size_t length = strlen(value);
        return SmvIntSerializer<size_t>::serializeValue(err, writer, length)
                && writer->write(err, value, length);
    }

    static bool serialize(IErrorHandler* err, IWriter* writer, const BufString_t& value) {
        return serialize(err, writer, value.buf);
    }

    static bool deserialize(IErrorHandler* err, IReader* reader, BufString_t& value_out) {
        size_t length;

        if (!SmvIntSerializer<size_t>::deserializeValue(err, reader, length))
            return false;

        ensureSize(err, value_out.buf, value_out.bufSize, length + 1);

        char next;

        // FIXME: check for overflow of size_t
        for (size_t have = 0; have < length; have++) {
            if (!reader->read(err, &next, sizeof(next)))
                return false;

            value_out.buf[have] = next;
        }

        value_out.buf[length] = 0;
        return true;
    }
};

#ifndef REFLECTOR_AVOID_STL
template <>
class Serializer<std::string> {
public:
    enum { TAG = TAG_UTF8 };

    static bool serialize(IErrorHandler* err, IWriter* writer, const std::string& value) {
        size_t length = value.length();
        return SmvIntSerializer<size_t>::serializeValue(err, writer, length)
                && writer->write(err, value.c_str(), length);
    }

    static bool deserialize(IErrorHandler* err, IReader* reader, std::string& value_out) {
        uint64_t length;

        if (!SmvIntSerializer<uint64_t>::deserializeValue(err, reader, length))
            return false;

        char next;

        value_out = "";

        // FIXME: check for overflow of size_t
        for (size_t have = 0; have < length; have++) {
            if (!reader->read(err, &next, sizeof(next)))
                return false;

            value_out.append(1, next);
        }

        return true;
    }
};

template <typename T>
class Serializer<std::vector<T>> {
public:
    enum { TAG = TAG_TYPED_ARRAY }; // FIXME

    static bool serialize(IErrorHandler* err, IWriter* writer, const std::vector<T>& value) {
        size_t length = value.size();
        if (!SmvIntSerializer<size_t>::serializeValue(err, writer, length))
            return false;

        for (size_t i = 0; i < length; i++)
            if (!Serializer<T>::serialize(err, writer, value[i]))
                return false;

        return true;
    }

    static bool deserialize(IErrorHandler* err, IReader* reader, std::vector<T>& value_out) {
        uint64_t length;

        if (!SmvIntSerializer<uint64_t>::deserializeValue(err, reader, length))
            return false;

        value_out.clear();

        for (size_t i = 0; i < length; i++)
        {
            value_out.emplace_back();

            if (!Serializer<T>::deserialize(err, reader, value_out[i]))
                return false;
        }

        return true;
    }
};
#endif

template <class C>
class InstanceSerializer {
public:
    enum { TAG = TAG_CLASS };

    template <typename Fields>
    static bool serializeInstance(IErrorHandler* err, IWriter* writer,
            const char* className, const Fields& fields) {
        for (size_t i = 0; i < fields.count(); i++) {
            const auto& field = fields[i];

            if (!field.serialize(err, writer))
                return false;
        }

        return true;
    }

    template <typename Fields>
    static bool deserializeInstance(IErrorHandler* err, IReader* reader,
            const char* className, Fields& fields) {
        for (size_t i = 0; i < fields.count(); i++) {
            auto field = fields[i];

            if (!field.deserialize(err, reader))
                return false;
        }

        return true;
    }

    template <typename Fields>
    static bool serializeSchema(IErrorHandler* err, IWriter* writer,
            const char* className, Fields& fields) {
        BufString_t cn, str;

        size_t numFields = fields.count();

        if (!Serializer<size_t>::serialize(err, writer, numFields))
            return false;

        for (size_t i = 0; i < fields.count(); i++) {
            const auto& field = fields[i];

            const char* name = field.name;
            className = field.className;

            bufStringSet(err, cn.buf, cn.bufSize, className, strlen(className));
            bufStringSet(err, str.buf, str.bufSize, name, strlen(name));

            if (!Serializer<BufString_t>::serialize(err, writer, cn)
                    || !Serializer<BufString_t>::serialize(err, writer, str)
                    || !field.refl->serializeTypeInformation(err, writer, nullptr))
                return false;
        }

        return true;
    }
};
}

#endif
