#ifdef __ANDROID__
#include "Platform/AndroidPlatform.h"
#include <jni.h>
#include <stdexcept>

namespace {
JavaVM* VM = nullptr;
jclass Platform = nullptr;
class Environment {
public:
    JNIEnv* Env = nullptr;
    bool Attached = false;
    Environment() {
        if (!VM || !Platform) throw std::runtime_error("Android platform bridge is unavailable");
        if (VM->GetEnv(reinterpret_cast<void**>(&Env), JNI_VERSION_1_6) == JNI_EDETACHED) {
            if (VM->AttachCurrentThread(&Env, nullptr) != JNI_OK) throw std::runtime_error("Could not attach Android worker thread");
            Attached = true;
        }
        if (!Env || Env->PushLocalFrame(32) != JNI_OK) throw std::runtime_error("Android JNI local frame allocation failed");
    }
    ~Environment() { Env->PopLocalFrame(nullptr); if (Attached) VM->DetachCurrentThread(); }
    void Check() {
        if (Env->ExceptionCheck()) {
            Env->ExceptionClear();
            throw std::runtime_error("Android platform storage operation failed");
        }
    }
    jmethodID Method(const char* name, const char* signature) {
        auto method = Env->GetStaticMethodID(Platform, name, signature);
        Check();
        if (!method) throw std::runtime_error("Android platform method is unavailable");
        return method;
    }
    jstring Text(const std::string& value) {
        // Paths originate in Android's private directories. Java decodes standard UTF-8 explicitly.
        auto bytes = Env->NewByteArray(static_cast<jsize>(value.size()));
        Env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(value.size()), reinterpret_cast<const jbyte*>(value.data()));
        auto result = static_cast<jstring>(Env->CallStaticObjectMethod(Platform, Method("Utf8", "([B)Ljava/lang/String;"), bytes));
        Check();
        return result;
    }
    std::string String(jstring value) {
        auto bytes = static_cast<jbyteArray>(Env->CallStaticObjectMethod(Platform, Method("Bytes", "(Ljava/lang/String;)[B"), value));
        Check();
        std::string result(Env->GetArrayLength(bytes), '\0');
        Env->GetByteArrayRegion(bytes, 0, static_cast<jsize>(result.size()), reinterpret_cast<jbyte*>(result.data()));
        Check();
        Env->DeleteLocalRef(bytes);
        return result;
    }
    std::vector<unsigned char> Data(jbyteArray bytes) {
        std::vector<unsigned char> result(Env->GetArrayLength(bytes));
        Env->GetByteArrayRegion(bytes, 0, static_cast<jsize>(result.size()), reinterpret_cast<jbyte*>(result.data()));
        Check();
        return result;
    }
};
std::string Key(const std::filesystem::path& path) {
    auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    auto type = env->FindClass("com/bazzalt/runtime/Platform");
    if (!type) return JNI_ERR;
    Platform = static_cast<jclass>(env->NewGlobalRef(type));
    env->DeleteLocalRef(type);
    VM = vm;
    return Platform ? JNI_VERSION_1_6 : JNI_ERR;
}

namespace Bazzalt::Runtime::AndroidPlatform {
void ShowIntro(bool enabled, int size, bool allowSkip) {
    Environment e;
    e.Env->CallStaticVoidMethod(Platform,e.Method("ShowIntro","(ZIZ)V"),enabled,size,allowSkip);
    e.Check();
}
bool IntroSkipped() {
    Environment e;
    auto result=e.Env->CallStaticBooleanMethod(Platform,e.Method("IntroSkipped","()Z"));
    e.Check();return result;
}
void FinishIntro() {
    Environment e;
    e.Env->CallStaticVoidMethod(Platform,e.Method("FinishIntro","()V"));
    e.Check();
}
std::filesystem::path ContentDirectory() {
    Environment e;
    auto result = static_cast<jstring>(e.Env->CallStaticObjectMethod(Platform, e.Method("ContentDirectory", "()Ljava/lang/String;")));
    e.Check();
    return std::filesystem::u8path(e.String(result));
}
std::filesystem::path NativeLibrary(const std::string& name) {
    Environment e;
    auto result = static_cast<jstring>(e.Env->CallStaticObjectMethod(Platform, e.Method("NativeLibrary", "(Ljava/lang/String;)Ljava/lang/String;"), e.Text(name)));
    e.Check();
    return std::filesystem::u8path(e.String(result));
}
std::vector<unsigned char> ProtectKey(const std::string& id, const std::vector<unsigned char>& seed) {
    Environment e;
    auto bytes = e.Env->NewByteArray(static_cast<jsize>(seed.size()));
    e.Env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(seed.size()), reinterpret_cast<const jbyte*>(seed.data()));
    auto result = static_cast<jbyteArray>(e.Env->CallStaticObjectMethod(Platform, e.Method("ProtectKey", "(Ljava/lang/String;[B)[B"), e.Text(id), bytes));
    e.Check();
    return e.Data(result);
}
std::optional<std::vector<unsigned char>> ReadSave(const std::filesystem::path& path) {
    Environment e;
    auto result = static_cast<jbyteArray>(e.Env->CallStaticObjectMethod(Platform, e.Method("ReadSave", "(Ljava/lang/String;)[B"), e.Text(Key(path))));
    e.Check();
    if (!result) return std::nullopt;
    return e.Data(result);
}
void WriteSave(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    Environment e;
    auto data = e.Env->NewByteArray(static_cast<jsize>(bytes.size()));
    e.Env->SetByteArrayRegion(data, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
    e.Env->CallStaticVoidMethod(Platform, e.Method("WriteSave", "(Ljava/lang/String;[B)V"), e.Text(Key(path)), data);
    e.Check();
}
bool RemoveSave(const std::filesystem::path& path) {
    Environment e;
    auto result = e.Env->CallStaticBooleanMethod(Platform, e.Method("RemoveSave", "(Ljava/lang/String;)Z"), e.Text(Key(path)));
    e.Check();
    return result;
}
std::vector<std::string> SaveFiles(const std::filesystem::path& directory) {
    Environment e;
    auto result = static_cast<jobjectArray>(e.Env->CallStaticObjectMethod(Platform, e.Method("SaveFiles", "(Ljava/lang/String;)[Ljava/lang/String;"), e.Text(Key(directory))));
    e.Check();
    std::vector<std::string> names;
    for (jsize i = 0; i < e.Env->GetArrayLength(result); ++i) {
        auto value = static_cast<jstring>(e.Env->GetObjectArrayElement(result, i));
        names.push_back(e.String(value));
        e.Env->DeleteLocalRef(value);
    }
    return names;
}
}
#endif
