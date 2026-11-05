#include "AndroidBridge.h"
#ifdef __ANDROID__
#include <SDL.h>
#include <jni.h>

namespace redbound {

static jclass activityClass(JNIEnv* env) {
	jobject act = (jobject)SDL_AndroidGetActivity();
	if (!act) return 0;
	jclass c = env->GetObjectClass(act);
	env->DeleteLocalRef(act);
	return c;
}

bool androidPickDeb() {
	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jclass c = env ? activityClass(env) : 0;
	if (!c) return false;
	jmethodID m = env->GetStaticMethodID(c, "pickDeb", "()V");
	if (m) env->CallStaticVoidMethod(c, m);
	env->DeleteLocalRef(c);
	return m != 0;
}

bool androidPicking() {
	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jclass c = env ? activityClass(env) : 0;
	if (!c) return false;
	jmethodID m = env->GetStaticMethodID(c, "isPicking", "()Z");
	const bool r = m && env->CallStaticBooleanMethod(c, m);
	env->DeleteLocalRef(c);
	return r;
}

std::string androidTakePicked() {
	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jclass c = env ? activityClass(env) : 0;
	if (!c) return "";
	std::string out;
	jmethodID m = env->GetStaticMethodID(c, "takePicked", "()Ljava/lang/String;");
	if (m) {
		jstring js = (jstring)env->CallStaticObjectMethod(c, m);
		if (js) {
			const char* u = env->GetStringUTFChars(js, 0);
			out = u;
			env->ReleaseStringUTFChars(js, u);
			env->DeleteLocalRef(js);
		}
	}
	env->DeleteLocalRef(c);
	return out;
}

std::string androidExternalDir() {
	const char* p = SDL_AndroidGetExternalStoragePath();
	return p ? p : "";
}

std::string androidResolveUrl(const std::string& url) {
	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jclass c = env ? activityClass(env) : 0;
	if (!c) return "";
	std::string body;
	jmethodID m = env->GetStaticMethodID(c, "resolveUrl", "(Ljava/lang/String;)Ljava/lang/String;");
	if (m) {
		jstring address = env->NewStringUTF(url.c_str());
		jstring result = (jstring)env->CallStaticObjectMethod(c, m, address);
		if (env->ExceptionCheck()) env->ExceptionClear();
		if (result) {
			const char* chars = env->GetStringUTFChars(result, 0);
			if (chars) body = chars;
			env->ReleaseStringUTFChars(result, chars);
			env->DeleteLocalRef(result);
		}
		env->DeleteLocalRef(address);
	}
	env->DeleteLocalRef(c);
	return body;
}

}
#else
namespace redbound {
bool androidPickDeb() { return false; }
bool androidPicking() { return false; }
std::string androidTakePicked() { return ""; }
std::string androidExternalDir() { return ""; }
std::string androidResolveUrl(const std::string&) { return ""; }
}
#endif
