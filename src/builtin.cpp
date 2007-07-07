#include "builtin.h"
#include "machine.h"
#include "run.h"

namespace vm {
namespace builtin {

jstring
toString(Thread* t, jobject this_)
{
  object s = makeString
    (t, "%s@%p",
     &byteArrayBody(t, className(t, objectClass(t, *this_)), 0),
     *this_);

  return pushReference(t, s);
}

void
wait(Thread* t, jobject this_, jlong milliseconds)
{
  vm::wait(t, *this_, milliseconds);
}

void
notify(Thread* t, jobject this_)
{
  vm::notify(t, *this_);
}

void
notifyAll(Thread* t, jobject this_)
{
  vm::notifyAll(t, *this_);
}

void
loadLibrary(Thread* t, jstring nameString)
{
  if (LIKELY(nameString)) {
    object n = *nameString;
    char name[stringLength(t, n) + 1];
    memcpy(name,
           &byteArrayBody(t, stringBytes(t, n), stringOffset(t, n)),
           stringLength(t, n));
    name[stringLength(t, n)] = 0;

    System::Library* lib;
    if (LIKELY(t->vm->system->success
               (t->vm->system->load(&lib, name, t->vm->libraries))))
    {
      t->vm->libraries = lib;
    } else {
      object message = makeString(t, "library not found: %s", name);
      t->exception = makeRuntimeException(t, message);
    }
  } else {
    t->exception = makeNullPointerException(t);
  }
}

void
arraycopy(Thread* t, jobject src, jint srcOffset, jobject dst, jint dstOffset,
          jint length)
{
  if (LIKELY(src and dst)) {
    object s = *src;
    object d = *dst;

    if (LIKELY(objectClass(t, s) == objectClass(t, d))) {
      unsigned elementSize = classArrayElementSize(t, objectClass(t, s));

      if (LIKELY(elementSize)) {
        unsigned offset = 0;

        if (objectClass(t, s)
            == arrayBody(t, t->vm->types, Machine::ObjectArrayType))
        {
          if (LIKELY(objectArrayElementClass(t, s)
                     == objectArrayElementClass(t, d)))
          {
            offset = 1;
          } else {
            t->exception = makeArrayStoreException(t);
            return;
          }
        }

        int32_t sl = cast<uint32_t>(s, offset * BytesPerWord);
        int32_t dl = cast<uint32_t>(d, offset * BytesPerWord);
        if (LIKELY(srcOffset >= 0 and srcOffset + length <= sl and
                   dstOffset >= 0 and dstOffset + length < dl))
        {
          uint8_t* sbody = &cast<uint8_t>(s, (offset * BytesPerWord) + 4);
          uint8_t* dbody = &cast<uint8_t>(s, (offset * BytesPerWord) + 4);
          memcpy(sbody + (srcOffset * elementSize),
                 dbody + (dstOffset * elementSize),
                 length * elementSize);
          return;
        }
      }
    }
  } else {
    t->exception = makeNullPointerException(t);
    return;
  }

  t->exception = makeArrayStoreException(t);
}

jarray
trace(Thread* t, jint skipCount)
{
  int frame = t->frame;
  while (skipCount-- and frame >= 0) {
    frame = frameNext(t, frame);
  }
  
  if (methodClass(t, frameMethod(t, frame))
      == arrayBody(t, t->vm->types, Machine::ThrowableType))
  {
    // skip Throwable constructors
    while (strcmp(reinterpret_cast<const int8_t*>("<init>"),
                  &byteArrayBody(t, methodName(t, frameMethod(t, frame)), 0))
           == 0)
    {
      frame = frameNext(t, frame);
    }
  }

  return pushReference(t, makeTrace(t, frame));
}

void
start(Thread* t, jobject this_)
{
  Thread* p = reinterpret_cast<Thread*>(threadPeer(t, *this_));
  if (p) {
    object message = makeString(t, "thread already started");
    t->exception = makeIllegalStateException(t, message);
  } else {
    p = new (t->vm->system->allocate(sizeof(Thread)))
      Thread(t->vm, t->vm->system, *this_, t);

    enter(p, Thread::ActiveState);

    class Runnable: public System::Runnable {
     public:
      Runnable(Thread* t): t(t) { }

      virtual void run(System::Thread* st) {
        t->systemThread = st;

        vm::run(t, "java/lang/Thread", "run", "()V", t->javaThread);

        t->exit();
      }

      Thread* t;
    } r(p);

    if (not t->vm->system->success(t->vm->system->start(&r))) {
      p->exit();

      object message = makeString(t, "unable to start native thread");
      t->exception = makeRuntimeException(t, message);
    }
  }
}
void
populate(Thread* t, object map)
{
  struct {
    const char* key;
    void* value;
  } builtins[] = {
    { "Java_java_lang_Object_toString",
      reinterpret_cast<void*>(toString) },
    { "Java_java_lang_Object_wait",
      reinterpret_cast<void*>(wait) },
    { "Java_java_lang_Object_notify",
      reinterpret_cast<void*>(notify) },
    { "Java_java_lang_Object_notifyAll",
      reinterpret_cast<void*>(notifyAll) },
    { "Java_java_lang_System_loadLibrary",
      reinterpret_cast<void*>(loadLibrary) },
    { "Java_java_lang_System_arraycopy",
      reinterpret_cast<void*>(arraycopy) },
    { "Java_java_lang_Throwable_trace",
      reinterpret_cast<void*>(trace) },
    { "Java_java_lang_Thread_start",
      reinterpret_cast<void*>(start) },
    { 0, 0 }
  };

  for (unsigned i = 0; builtins[i].key; ++i) {
    object key = makeByteArray(t, builtins[i].key);
    PROTECT(t, key);
    object value = makePointer(t, builtins[i].value);

    hashMapInsert(t, map, key, value, byteArrayHash);
  }
}

} // namespace builtin
} // namespace vm
