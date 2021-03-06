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

#pragma once

#include "bufstring.hpp"
#include "base.hpp"

#include <type_traits>

#ifndef REFLECTOR_AVOID_STL
#include <string>
#endif

namespace reflection {  // UUID('c3549467-1615-4087-9829-176a2dc44b76')

extern IErrorHandler* err;

template <typename From, typename To>
struct copy_const {
    typedef To type;
};

template <typename From, typename To>
struct copy_const<const From, To> {
    typedef std::add_const<To> type;
};

template <typename Ptr_t>
class ReflectedFields {
public:
    struct Field : Field_t {
        const char* className;
        Ptr_t inst;
        void* field;

        Field(const Field_t& field_in, const char* className, Ptr_t inst)
            : Field_t(field_in), className(className), inst(inst) {
            field = fieldGetter(inst);
        }

        void* ptr() { return field; }
        const void* ptr() const { return field; }

        bool deserialize(serialization::IReader* reader) { return refl->deserialize(err, reader, field); }
        bool deserialize(IErrorHandler* err, serialization::IReader* reader) { return refl->deserialize(err, reader, field); }

        bool serialize(serialization::IWriter* writer) const { return refl->serialize(err, writer, field); }
        bool serialize(IErrorHandler* err, serialization::IWriter* writer) const { return refl->serialize(err, writer, field); }

        bool isPolymorphic() const { return refl->isPolymorphic(); }
        template <typename T> bool isType() const { return refl == reflectionForType2<T>(); }
        const char* staticTypeName() const { return refl->staticTypeName(); }
        bool toString(char*& buf, size_t& bufSize) const { return refl->toString(err, buf, bufSize, FIELD_STATE, field); }
        bool toString(IErrorHandler* err, char*& buf, size_t& bufSize) const { return refl->toString(err, buf, bufSize, FIELD_STATE, field); }
        const char* typeName() const { return refl->typeName(field); }
        bool setFromString(IErrorHandler* err, const char* str) { return refl->setFromString(err, str, strlen(str), field); }

#ifndef REFLECTOR_AVOID_STL
        bool setFromString(const std::string& str) { return refl->setFromString(err, str.c_str(), str.length(), field); }

        std::string toString() const {
            char* buf = nullptr;
            size_t bufSize = 0;
            AllocGuard guard(buf);

            if (!refl->toString(err, buf, bufSize, FIELD_STATE, field))
                return "";

            return buf;
        }
#endif
    };

    ReflectedFields(Ptr_t inst, FieldSet_t const* fieldSet)
            : inst(inst), fieldSet(fieldSet) {
        // count all fields including base class(es)
        numFields = 0;
        for (FieldSet_t const* p_fieldSet = fieldSet; p_fieldSet != nullptr; p_fieldSet = p_fieldSet->baseClassFields) {
            numFields += p_fieldSet->numFields;
        }
    }

    typename copy_const<Ptr_t, Field>::type operator [] (size_t index) const {
        FieldSet_t const* p_fieldSet = fieldSet;
        Ptr_t p_inst = inst;

        while (true) {
            // perhaps the field is in this class?
            if (index < p_fieldSet->numFields)
                return Field(p_fieldSet->fields[index], p_fieldSet->className, p_inst);

            // apparently not; search next base class
            index -= p_fieldSet->numFields;
            p_inst = p_fieldSet->derivedPtrToBasePtr(const_cast<void*>(p_inst));
            p_fieldSet = p_fieldSet->baseClassFields;
        }
    }

    size_t count() const {
        return numFields;
    }

    Ptr_t inst;
    FieldSet_t const* fieldSet;
    size_t numFields;
};

template <typename T>
const char* reflectClassName(T& inst) {
    return inst.reflection_className(REFL_MATCH);
}

template <typename T>
ReflectedFields<void*> reflectFields(T& inst) {
    return ReflectedFields<void*>(reinterpret_cast<void*>(&inst), inst.reflection_getFields(REFL_MATCH));
}

template <typename T>
ReflectedFields<const void*> reflectFields(const T& inst) {
    return ReflectedFields<const void*>(reinterpret_cast<const void*>(&inst), inst.reflection_getFields(REFL_MATCH));
}

template <typename C>
ReflectedFields<void*> reflectFieldsStatic() {
    return ReflectedFields<void*>(nullptr, C::template reflection_s_getFields<C>(REFL_MATCH));
}

template <typename T>
void reflectPrint(T& instance, uint32_t fieldMask = FIELD_STATE | FIELD_CONFIG) {
    const char* className = reflectClassName(instance);
    printf("Instance of class %s:\n", className);

    auto fields = reflectFields(instance);

    for (size_t i = 0; i < fields.count(); i++) {
        const auto& field = fields[i];

        if (!(field.systemFlags & fieldMask))
            continue;

        printf("%-15s %s::%s = %s\n", field.typeName(), field.className, field.name, field.toString().c_str());

        if (field.isPolymorphic())
            printf("\t(declared field type: %s)\n", field.staticTypeName());
    }

    printf("\n");
}

// ====================================================================== //
//  reflectSerialize
// ====================================================================== //

template <typename T>
bool reflectSerialize(const T& inst, serialization::IWriter* writer) {
    ITypeReflection* refl = reflectionForType2<T>();

    return refl->serialize(err, writer, reinterpret_cast<const void*>(&inst));
}

// ====================================================================== //
//  reflectDeserialize
// ====================================================================== //

template <typename T>
bool reflectDeserialize(T& value_out, serialization::IReader* reader) {
    ITypeReflection* refl = reflectionForType2<T>();

    return refl->deserialize(err, reader, reinterpret_cast<void*>(&value_out));
}

// ====================================================================== //
//  reflectToString
// ====================================================================== //

#ifndef REFLECTOR_AVOID_STL
inline std::string reflectToString(const ReflectedValue_t& val, uint32_t fieldMask = FIELD_STATE) {
    char* buf = nullptr;
    size_t bufSize = 0;

    if (!val.refl->toString(err, buf, bufSize, fieldMask, val.p_value))
        return "";

    std::string str(buf);
    free(buf);
    return std::move(str);
}

template <typename T>
std::string reflectToString(const T inst[], uint32_t fieldMask = FIELD_STATE) {
    static_assert(T() && false, "reflectToString currently doesn't work with arrays");

    return std::string();
}

template <typename T>
std::string reflectToString(const T& inst, uint32_t fieldMask = FIELD_STATE) {
    ITypeReflection* refl = reflectionForType(inst);

    char* buf = nullptr;
    size_t bufSize = 0;

    if (!refl->toString(err, buf, bufSize, fieldMask, reinterpret_cast<const void*>(&inst)))
        return "";

    std::string str(buf);
    free(buf);
    return std::move(str);
}
#endif

// ====================================================================== //
//  reflectFromString
// ====================================================================== //

#ifndef REFLECTOR_AVOID_STL
inline bool reflectFromString(ReflectedValue_t& val, const std::string& str) {
    return val.refl->setFromString(err, str.c_str(), str.length(), val.p_value);
}

template <typename T>
bool reflectFromString(T& inst, const std::string& str) {
    ITypeReflection* refl = reflectionForType(inst);

    return refl->setFromString(err, str.c_str(), str.length(), reinterpret_cast<void*>(&inst));
}
#endif

template <typename T>
const char* reflectTypeName() {
    auto refl = reflectionForType2<T>();

    return refl->staticTypeName();
}

template <typename T>
const char* reflectTypeName(T& inst) {
    auto refl = reflectionForType2<T>();

    return refl->typeName(reinterpret_cast<const void*>(&inst));
}

template <class C>
const UUID_t& uuidOfClass() {
    return C::reflection_s_uuid(REFL_MATCH);
}

template <class C>
const char* versionedNameOfClass() {
    return C::reflection_s_classId(REFL_MATCH);
}
}
