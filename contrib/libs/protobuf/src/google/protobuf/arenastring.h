// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// https://developers.google.com/protocol-buffers/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef GOOGLE_PROTOBUF_ARENASTRING_H__
#define GOOGLE_PROTOBUF_ARENASTRING_H__

#include <string>
#include <type_traits>
#include <utility>

#include <google/protobuf/stubs/logging.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/arena.h>
#include <google/protobuf/port.h>

#include <google/protobuf/port_def.inc>

#ifdef SWIG
#error "You cannot SWIG proto headers"
#endif


namespace google {
namespace protobuf {
namespace internal {

template <typename T>
class ExplicitlyConstructed;

class SwapFieldHelper;

// Lazy string instance to support string fields with non-empty default.
// These are initialized on the first call to .get().
class PROTOBUF_EXPORT LazyString {
 public:
  // We explicitly make LazyString an aggregate so that MSVC can do constant
  // initialization on it without marking it `constexpr`.
  // We do not want to use `constexpr` because it makes it harder to have extern
  // storage for it and causes library bloat.
  struct InitValue {
    const char* ptr;
    size_t size;
  };
  // We keep a union of the initialization value and the TProtoStringType to save on
  // space. We don't need the string array after Init() is done.
  union {
    mutable InitValue init_value_;
    alignas(TProtoStringType) mutable char string_buf_[sizeof(TProtoStringType)];
  };
  mutable std::atomic<const TProtoStringType*> inited_;

  const TProtoStringType& get() const {
    // This check generates less code than a call-once invocation.
    auto* res = inited_.load(std::memory_order_acquire);
    if (PROTOBUF_PREDICT_FALSE(res == nullptr)) return Init();
    return *res;
  }

 private:
  // Initialize the string in `string_buf_`, update `inited_` and return it.
  // We return it here to avoid having to read it again in the inlined code.
  const TProtoStringType& Init() const;
};

template <typename T>
class TaggedPtr {
 public:
  TaggedPtr() = default;
  explicit constexpr TaggedPtr(const ExplicitlyConstructed<TProtoStringType>* ptr)
      : ptr_(const_cast<ExplicitlyConstructed<TProtoStringType>*>(ptr)) {}

  void SetTagged(T* p) {
    Set(p);
    ptr_ = reinterpret_cast<void*>(as_int() | 1);
  }
  void Set(T* p) { ptr_ = p; }
  T* Get() const { return reinterpret_cast<T*>(as_int() & -2); }
  bool IsTagged() const { return as_int() & 1; }

  // Returned value is only safe to dereference if IsTagged() == false.
  // It is safe to compare.
  T* UnsafeGet() const { return static_cast<T*>(ptr_); }

  bool IsNull() { return ptr_ == nullptr; }

 private:
  uintptr_t as_int() const { return reinterpret_cast<uintptr_t>(ptr_); }
  void* ptr_;
};

static_assert(std::is_trivial<TaggedPtr<TProtoStringType>>::value,
              "TaggedPtr must be trivial");

// This class encapsulates a pointer to a TProtoStringType with or without a donated
// buffer, tagged by bottom bit. It is a high-level wrapper that almost directly
// corresponds to the interface required by string fields in generated
// code. It replaces the old TProtoStringType* pointer in such cases.
//
// The object has different but similar code paths for when the default value is
// the empty string and when it is a non-empty string.
// The empty string is handled different throughout the library and there is a
// single global instance of it we can share.
//
// For fields with an empty string default value, there are three distinct
// states:
//
// - Pointer set to 'String' tag (LSB is 0), equal to
//   &GetEmptyStringAlreadyInited(): field is set to its default value. Points
//   to a true TProtoStringType*, but we do not own that TProtoStringType* (it's a
//   globally shared instance).
//
// - Pointer set to 'String' tag (LSB is 0), but not equal to the global empty
//   string: field points to a true TProtoStringType* instance that we own. This
//   instance is either on the heap or on the arena (i.e. registered on
//   free()/destructor-call list) as appropriate.
//
// - Pointer set to 'DonatedString' tag (LSB is 1): points to a TProtoStringType
//   instance with a buffer on the arena (arena != NULL, always, in this case).
//
// For fields with a non-empty string default value, there are three distinct
// states:
//
// - Pointer set to 'String' tag (LSB is 0), equal to `nullptr`:
//   Field is in "default" mode and does not point to any actual instance.
//   Methods that might need to create an instance of the object will pass a
//   `const LazyString&` for it.
//
// - Pointer set to 'String' tag (LSB is 0), but not equal to `nullptr`:
//   field points to a true TProtoStringType* instance that we own. This instance is
//   either on the heap or on the arena (i.e. registered on
//   free()/destructor-call list) as appropriate.
//
// - Pointer set to 'DonatedString' tag (LSB is 1): points to a TProtoStringType
//   instance with a buffer on the arena (arena != NULL, always, in this case).
//
// Generated code and reflection code both ensure that ptr_ is never null for
// fields with an empty default.
// Because ArenaStringPtr is used in oneof unions, its constructor is a NOP and
// so the field is always manually initialized via method calls.
//
// Side-note: why pass information about the default on every API call? Because
// we don't want to hold it in a member variable, or else this would go into
// every proto message instance. This would be a huge waste of space, since the
// default instance pointer is typically a global (static class field). We want
// the generated code to be as efficient as possible, and if we take
// the default value information as a parameter that's in practice taken from a
// static class field, and compare ptr_ to the default value, we end up with a
// single "cmp %reg, GLOBAL" in the resulting machine code. (Note that this also
// requires the String tag to be 0 so we can avoid the mask before comparing.)
struct PROTOBUF_EXPORT ArenaStringPtr {
  ArenaStringPtr() = default;
  explicit constexpr ArenaStringPtr(
      const ExplicitlyConstructed<TProtoStringType>* default_value)
      : tagged_ptr_(default_value) {}

  // Some methods below are overloaded on a `default_value` and on tags.
  // The tagged overloads help reduce code size in the callers in generated
  // code, while the `default_value` overloads are useful from reflection.
  // By-value empty struct arguments are elided in the ABI.
  struct EmptyDefault {};
  struct NonEmptyDefault {};

  void Set(const TProtoStringType* default_value, ConstStringParam value,
           ::google::protobuf::Arena* arena);
  void Set(const TProtoStringType* default_value, TProtoStringType&& value,
           ::google::protobuf::Arena* arena);
  void Set(EmptyDefault, ConstStringParam value, ::google::protobuf::Arena* arena);
  void Set(EmptyDefault, TProtoStringType&& value, ::google::protobuf::Arena* arena);
  void Set(NonEmptyDefault, ConstStringParam value, ::google::protobuf::Arena* arena);
  void Set(NonEmptyDefault, TProtoStringType&& value, ::google::protobuf::Arena* arena);
  template <typename FirstParam>
  void Set(FirstParam p1, const char* str, ::google::protobuf::Arena* arena) {
    Set(p1, ConstStringParam(str), arena);
  }
  template <typename FirstParam>
  void Set(FirstParam p1, const char* str, size_t size,
           ::google::protobuf::Arena* arena) {
    ConstStringParam sp{str, size};  // for string_view and `const string &`
    Set(p1, sp, arena);
  }
  template <typename FirstParam, typename RefWrappedType>
  void Set(FirstParam p1,
           std::reference_wrapper<RefWrappedType> const_string_ref,
           ::google::protobuf::Arena* arena) {
    Set(p1, const_string_ref.get(), arena);
  }

  template <typename FirstParam, typename SecondParam>
  void SetBytes(FirstParam p1, SecondParam&& p2, ::google::protobuf::Arena* arena) {
    Set(p1, static_cast<SecondParam&&>(p2), arena);
  }
  template <typename FirstParam>
  void SetBytes(FirstParam p1, const void* str, size_t size,
                ::google::protobuf::Arena* arena) {
    // must work whether ConstStringParam is string_view or `const string &`
    ConstStringParam sp{static_cast<const char*>(str), size};
    Set(p1, sp, arena);
  }

  // Basic accessors.
  PROTOBUF_NDEBUG_INLINE const TProtoStringType& Get() const {
    // Unconditionally mask away the tag.
    return *tagged_ptr_.Get();
  }
  PROTOBUF_NDEBUG_INLINE const TProtoStringType* GetPointer() const {
    // Unconditionally mask away the tag.
    return tagged_ptr_.Get();
  }

  // For fields with an empty default value.
  TProtoStringType* Mutable(EmptyDefault, ::google::protobuf::Arena* arena);
  // For fields with a non-empty default value.
  TProtoStringType* Mutable(const LazyString& default_value, ::google::protobuf::Arena* arena);

  // Release returns a TProtoStringType* instance that is heap-allocated and is not
  // Own()'d by any arena. If the field is not set, this returns NULL. The
  // caller retains ownership. Clears this field back to NULL state. Used to
  // implement release_<field>() methods on generated classes.
  PROTOBUF_MUST_USE_RESULT TProtoStringType* Release(
      const TProtoStringType* default_value, ::google::protobuf::Arena* arena);
  PROTOBUF_MUST_USE_RESULT TProtoStringType* ReleaseNonDefault(
      const TProtoStringType* default_value, ::google::protobuf::Arena* arena);

  // Takes a TProtoStringType that is heap-allocated, and takes ownership. The
  // TProtoStringType's destructor is registered with the arena. Used to implement
  // set_allocated_<field> in generated classes.
  void SetAllocated(const TProtoStringType* default_value, TProtoStringType* value,
                    ::google::protobuf::Arena* arena);

  // Swaps internal pointers. Arena-safety semantics: this is guarded by the
  // logic in Swap()/UnsafeArenaSwap() at the message level, so this method is
  // 'unsafe' if called directly.
  inline PROTOBUF_NDEBUG_INLINE static void InternalSwap(
      const TProtoStringType* default_value, ArenaStringPtr* rhs, Arena* rhs_arena,
      ArenaStringPtr* lhs, Arena* lhs_arena);

  // Frees storage (if not on an arena).
  void Destroy(const TProtoStringType* default_value, ::google::protobuf::Arena* arena);
  void Destroy(EmptyDefault, ::google::protobuf::Arena* arena);
  void Destroy(NonEmptyDefault, ::google::protobuf::Arena* arena);

  // Clears content, but keeps allocated TProtoStringType, to avoid the overhead of
  // heap operations. After this returns, the content (as seen by the user) will
  // always be the empty TProtoStringType. Assumes that |default_value| is an empty
  // TProtoStringType.
  void ClearToEmpty();

  // Clears content, assuming that the current value is not the empty
  // string default.
  void ClearNonDefaultToEmpty();

  // Clears content, but keeps allocated TProtoStringType if arena != NULL, to avoid
  // the overhead of heap operations. After this returns, the content (as seen
  // by the user) will always be equal to |default_value|.
  void ClearToDefault(const LazyString& default_value, ::google::protobuf::Arena* arena);

  // Called from generated code / reflection runtime only. Resets value to point
  // to a default string pointer, with the semantics that this
  // ArenaStringPtr does not own the pointed-to memory. Disregards initial value
  // of ptr_ (so this is the *ONLY* safe method to call after construction or
  // when reinitializing after becoming the active field in a oneof union).
  inline void UnsafeSetDefault(const TProtoStringType* default_value);

  // Returns a mutable pointer, but doesn't initialize the string to the
  // default value.
  TProtoStringType* MutableNoArenaNoDefault(const TProtoStringType* default_value);

  // Get a mutable pointer with unspecified contents.
  // Similar to `MutableNoArenaNoDefault`, but also handles the arena case.
  // If the value was donated, the contents are discarded.
  TProtoStringType* MutableNoCopy(const TProtoStringType* default_value,
                             ::google::protobuf::Arena* arena);

  // Destroy the string. Assumes `arena == nullptr`.
  void DestroyNoArena(const TProtoStringType* default_value);

  // Internal setter used only at parse time to directly set a donated string
  // value.
  void UnsafeSetTaggedPointer(TaggedPtr<TProtoStringType> value) {
    tagged_ptr_ = value;
  }
  // Generated code only! An optimization, in certain cases the generated
  // code is certain we can obtain a TProtoStringType with no default checks and
  // tag tests.
  TProtoStringType* UnsafeMutablePointer() PROTOBUF_RETURNS_NONNULL;

  inline bool IsDefault(const TProtoStringType* default_value) const {
    // Relies on the fact that kPtrTagString == 0, so if IsString(), ptr_ is the
    // actual TProtoStringType pointer (and if !IsString(), ptr_ will never be equal
    // to any aligned |default_value| pointer). The key is that we want to avoid
    // masking in the fastpath const-pointer Get() case for non-arena code.
    return tagged_ptr_.UnsafeGet() == default_value;
  }

 private:
  TaggedPtr<TProtoStringType> tagged_ptr_;

  bool IsDonatedString() const { return false; }

  // Swaps tagged pointer without debug hardening. This is to allow python
  // protobuf to maintain pointer stability even in DEBUG builds.
  inline PROTOBUF_NDEBUG_INLINE static void UnsafeShallowSwap(
      ArenaStringPtr* rhs, ArenaStringPtr* lhs) {
    std::swap(lhs->tagged_ptr_, rhs->tagged_ptr_);
  }

  friend class ::google::protobuf::internal::SwapFieldHelper;

  // Slow paths.

  // MutableSlow requires that !IsString() || IsDefault
  // Variadic to support 0 args for EmptyDefault and 1 arg for LazyString.
  template <typename... Lazy>
  TProtoStringType* MutableSlow(::google::protobuf::Arena* arena, const Lazy&... lazy_default);

  // Sets value to a newly allocated string and returns it
  TProtoStringType* SetAndReturnNewString();

  // Destroys the non-default string value out-of-line
  void DestroyNoArenaSlowPath();

};

inline void ArenaStringPtr::UnsafeSetDefault(const TProtoStringType* value) {
  tagged_ptr_.Set(const_cast<TProtoStringType*>(value));
}

inline PROTOBUF_NDEBUG_INLINE void ArenaStringPtr::InternalSwap(  //
    const TProtoStringType* default_value,                             //
    ArenaStringPtr* rhs, Arena* rhs_arena,                        //
    ArenaStringPtr* lhs, Arena* lhs_arena) {
  (void)default_value;
  std::swap(lhs_arena, rhs_arena);
  std::swap(lhs->tagged_ptr_, rhs->tagged_ptr_);
#ifdef PROTOBUF_FORCE_COPY_IN_SWAP
  auto force_realloc = [default_value](ArenaStringPtr* p, Arena* arena) {
    if (p->IsDefault(default_value)) return;
    TProtoStringType* old_value = p->tagged_ptr_.Get();
    TProtoStringType* new_value =
        p->IsDonatedString()
            ? Arena::Create<TProtoStringType>(arena, *old_value)
            : Arena::Create<TProtoStringType>(arena, std::move(*old_value));
    if (arena == nullptr) delete old_value;
    p->tagged_ptr_.Set(new_value);
  };
  force_realloc(lhs, lhs_arena);
  force_realloc(rhs, rhs_arena);
#endif  // PROTOBUF_FORCE_COPY_IN_SWAP
}

inline void ArenaStringPtr::ClearNonDefaultToEmpty() {
  // Unconditionally mask away the tag.
  tagged_ptr_.Get()->clear();
}

inline TProtoStringType* ArenaStringPtr::MutableNoArenaNoDefault(
    const TProtoStringType* default_value) {
  // VERY IMPORTANT for performance and code size: this will reduce to a member
  // variable load, a pointer check (against |default_value|, in practice a
  // static global) and a branch to the slowpath (which calls operator new and
  // the ctor). DO NOT add any tagged-pointer operations here.
  if (IsDefault(default_value)) {
    return SetAndReturnNewString();
  } else {
    return UnsafeMutablePointer();
  }
}

inline void ArenaStringPtr::DestroyNoArena(const TProtoStringType* default_value) {
  if (!IsDefault(default_value)) {
    DestroyNoArenaSlowPath();
  }
}

inline TProtoStringType* ArenaStringPtr::UnsafeMutablePointer() {
  GOOGLE_DCHECK(!tagged_ptr_.IsTagged());
  GOOGLE_DCHECK(tagged_ptr_.UnsafeGet() != nullptr);
  return tagged_ptr_.UnsafeGet();
}


}  // namespace internal
}  // namespace protobuf
}  // namespace google

#include <google/protobuf/port_undef.inc>

#endif  // GOOGLE_PROTOBUF_ARENASTRING_H__
