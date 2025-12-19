/**
 * Copyright (c) 2017-2024 m2049r
 * <p>
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * <p>
 * http://www.apache.org/licenses/LICENSE-2.0
 * <p>
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <inttypes.h>
#include <string>
#include <vector>
#include <mutex>
#include <cassert>
#include <cstring>
#include <jni.h>

#include "monerujo.h"
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>

#include "wallet2_api.h"

// ADD LOGGING MACROS HERE (BEFORE ANY FUNCTIONS THAT USE THEM)
#include <android/log.h>
#define LOG_TAG "WalletNDK"
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG,__VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG  , LOG_TAG,__VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO   , LOG_TAG,__VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN   , LOG_TAG,__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR  , LOG_TAG,__VA_ARGS__)
#define LOG_TID(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "[tid:%ld] " __VA_ARGS__, (long)gettid())

// Missing constants
#define HASH_SIZE 32
#ifndef MONERO_VERSION
#define MONERO_VERSION "1.0.0"
#endif

// Error codes for device query
#define WALLET_DEVICE_QUERY_ERROR -1
#define WALLET_DEVICE_NOT_FOUND -2

// Missing function declarations
extern "C" {
    void slow_hash(const void *data, size_t length, char *hash);
    void slow_hash_broken(const void *data, char *hash, int variant);
}

// Global class references
jclass class_ArrayList = nullptr;
jclass class_WalletListener = nullptr;
jclass class_TransactionInfo = nullptr;
jclass class_Transfer = nullptr;
jclass class_Ledger = nullptr;
jclass class_WalletStatus = nullptr;
jclass class_BluetoothService = nullptr;

static JavaVM *cachedJVM = nullptr;
std::mutex _listenerMutex;

// Add this global map to track wallet instances
static std::unordered_map<Monero::Wallet*, std::string> g_walletTracker;
static std::mutex g_walletTrackerMutex;

// RAII wrapper for JNI strings - MOVED TO TOP
class JNIStringGuard {
    JNIEnv* env;
    jstring jstr;
    const char* cstr;
    bool released;
    
public:
    JNIStringGuard(JNIEnv* e, jstring s) : env(e), jstr(s), cstr(nullptr), released(false) {
        LOGD("JNIStringGuard: Constructor - env: %p, jstr: %p", env, jstr);
        if (s != nullptr && env != nullptr) {
            try {
                cstr = env->GetStringUTFChars(s, nullptr);
                LOGD("JNIStringGuard: Got UTF chars: %p, valid: %d", 
                     cstr, (cstr != nullptr));
            } catch (...) {
                LOGE("JNIStringGuard: EXCEPTION in GetStringUTFChars");
                cstr = nullptr;
            }
        } else {
            LOGW("JNIStringGuard: null env(%p) or jstr(%p)", env, jstr);
        }
    }
    
    ~JNIStringGuard() { 
        LOGD("JNIStringGuard: Destructor - cstr: %p, released: %d", cstr, released);
        if (cstr != nullptr && env != nullptr && !released) {
            try {
                env->ReleaseStringUTFChars(jstr, cstr);
                released = true;
                LOGD("JNIStringGuard: Released UTF chars");
            } catch (...) {
                LOGE("JNIStringGuard: EXCEPTION in ReleaseStringUTFChars");
            }
        }
        cstr = nullptr;
    }
    
    const char* get() const { 
        LOGD("JNIStringGuard: get() - returning %p", cstr);
        return cstr; 
    }
    
    bool isValid() const { 
        bool valid = (cstr != nullptr && env != nullptr);
        LOGD("JNIStringGuard: isValid() - %d (cstr: %p, env: %p)", 
             valid, cstr, env);
        return valid;
    }
    
    // Prevent copying
    JNIStringGuard(const JNIStringGuard&) = delete;
    JNIStringGuard& operator=(const JNIStringGuard&) = delete;
};

// Forward declarations for helper functions (NO DEFAULT PARAMETERS HERE)
// Note: getHandle is already defined in monerujo.h, so we don't redeclare it here
std::vector<std::string> java2cpp(JNIEnv *env, jobject arrayList);
jobject cpp2java(JNIEnv *env, const std::vector<std::string> &vector);
jobject newWalletStatusInstance(JNIEnv *env, int status, const std::string &errorString);
jobject newTransferInstance(JNIEnv *env, uint64_t amount, const std::string &address);
jobject newTransferList(JNIEnv *env, Monero::TransactionInfo *info);
jobject newTransactionInfo(JNIEnv *env, Monero::TransactionInfo *info);
jobject transactionInfoArrayList(JNIEnv *env, const std::vector<Monero::TransactionInfo *> &vector, uint32_t accountIndex);
void logWalletCreation(Monero::Wallet* wallet, const char* method);
void logWalletDeletion(Monero::Wallet* wallet);
void logAllWallets();

// JNI lifecycle functions
extern "C" {
    
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved) {
    cachedJVM = jvm;
    LOGI("JNI_OnLoad");
    JNIEnv *jenv;
    if (jvm->GetEnv(reinterpret_cast<void **>(&jenv), JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }

    class_ArrayList = static_cast<jclass>(jenv->NewGlobalRef(
            jenv->FindClass("java/util/ArrayList")));
    class_TransactionInfo = static_cast<jclass>(jenv->NewGlobalRef(
            jenv->FindClass("com/m2049r/xmrwallet/model/TransactionInfo")));
    class_Transfer = static_cast<jclass>(jenv->NewGlobalRef(
            jenv->FindClass("com/m2049r/xmrwallet/model/Transfer")));
    class_WalletListener = static_cast<jclass>(jenv->NewGlobalRef(
            jenv->FindClass("com/m2049r/xmrwallet/model/WalletListener")));
    class_Ledger = static_cast<jclass>(jenv->NewGlobalRef(
            jenv->FindClass("com/m2049r/xmrwallet/ledger/Ledger")));
    class_WalletStatus = static_cast<jclass>(jenv->NewGlobalRef(
            jenv->FindClass("com/m2049r/xmrwallet/model/Wallet$Status")));
    class_BluetoothService = static_cast<jclass>(jenv->NewGlobalRef(
            jenv->FindClass("com/m2049r/xmrwallet/service/BluetoothService")));
    // REMOVED: class_SidekickService initialization (Sidekick disabled)
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *jvm, void *reserved) {
    JNIEnv *env;
    if (jvm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        return;
    }
    
    if (class_ArrayList != nullptr) {
        env->DeleteGlobalRef(class_ArrayList);
        class_ArrayList = nullptr;
    }
    if (class_TransactionInfo != nullptr) {
        env->DeleteGlobalRef(class_TransactionInfo);
        class_TransactionInfo = nullptr;
    }
    if (class_Transfer != nullptr) {
        env->DeleteGlobalRef(class_Transfer);
        class_Transfer = nullptr;
    }
    if (class_WalletListener != nullptr) {
        env->DeleteGlobalRef(class_WalletListener);
        class_WalletListener = nullptr;
    }
    if (class_Ledger != nullptr) {
        env->DeleteGlobalRef(class_Ledger);
        class_Ledger = nullptr;
    }
    if (class_WalletStatus != nullptr) {
        env->DeleteGlobalRef(class_WalletStatus);
        class_WalletStatus = nullptr;
    }
    if (class_BluetoothService != nullptr) {
        env->DeleteGlobalRef(class_BluetoothService);
        class_BluetoothService = nullptr;
    }
    
    cachedJVM = nullptr;
}

// JVM attachment/detachment functions
int attachJVM(JNIEnv **jenv) {
    if (cachedJVM == nullptr) {
        LOGE("attachJVM: ERROR - No cached JVM");
        return JNI_ERR;
    }
    
    LOGD("attachJVM: cachedJVM: %p, thread: %ld", 
         cachedJVM, (long)gettid());
    
    int envStat = cachedJVM->GetEnv((void **) jenv, JNI_VERSION_1_6);
    LOGD("attachJVM: GetEnv status: %d", envStat);
    
    if (envStat == JNI_EDETACHED) {
        LOGD("attachJVM: Thread detached, attaching...");
        if (cachedJVM->AttachCurrentThread(jenv, nullptr) != 0) {
            LOGE("attachJVM: ERROR - Failed to attach thread");
            return JNI_ERR;
        }
        LOGD("attachJVM: Successfully attached thread");
    } else if (envStat == JNI_EVERSION) {
        LOGE("attachJVM: ERROR - JNI version not supported");
        return JNI_ERR;
    } else if (envStat == JNI_OK) {
        LOGD("attachJVM: Thread already attached");
    }
    
    return envStat;
}

void detachJVM(JNIEnv *jenv, int envStat) {
    if (jenv == nullptr) return;
    
    LOGD("detachJVM: env: %p, envStat: %d, thread: %ld", 
         jenv, envStat, (long)gettid());
    
    if (jenv->ExceptionCheck()) {
        LOGW("detachJVM: Exception pending, describing...");
        jenv->ExceptionDescribe();
        jenv->ExceptionClear();
    }

    if (envStat == JNI_EDETACHED) {
        LOGD("detachJVM: Detaching thread...");
        cachedJVM->DetachCurrentThread();
        LOGD("detachJVM: Thread detached");
    }
}

// Wallet Listener class
class MyWalletListener : public Monero::WalletListener {
private:
    jobject jlistener;
public:
    MyWalletListener(JNIEnv *env, jobject aListener) {
        LOGD("Created MyListener");
        jlistener = env->NewGlobalRef(aListener);
    }

    ~MyWalletListener() {
        LOGD("Destroyed MyListener");
        if (cachedJVM != nullptr && jlistener != nullptr) {
            JNIEnv *env;
            int envStat = attachJVM(&env);
            if (envStat != JNI_ERR) {
                env->DeleteGlobalRef(jlistener);
                detachJVM(env, envStat);
            }
        }
    };

    void deleteGlobalJavaRef(JNIEnv *env) {
        std::lock_guard<std::mutex> lock(_listenerMutex);
        if (jlistener != nullptr && env != nullptr) {
            env->DeleteGlobalRef(jlistener);
            jlistener = nullptr;
        }
    }

    void updated() {
        std::lock_guard<std::mutex> lock(_listenerMutex);
        if (jlistener == nullptr) return;
        LOGD("updated");
        JNIEnv *jenv;
        int envStat = attachJVM(&jenv);
        if (envStat == JNI_ERR) return;

        jmethodID listenerClass_updated = jenv->GetMethodID(class_WalletListener, "updated", "()V");
        if (listenerClass_updated != nullptr) {
            jenv->CallVoidMethod(jlistener, listenerClass_updated);
        }

        detachJVM(jenv, envStat);
    }

    void moneySpent(const std::string &txId, uint64_t amount) {
        std::lock_guard<std::mutex> lock(_listenerMutex);
        if (jlistener == nullptr) return;
        LOGD("moneySpent %" PRIu64, amount);
    }

    void moneyReceived(const std::string &txId, uint64_t amount) {
        std::lock_guard<std::mutex> lock(_listenerMutex);
        if (jlistener == nullptr) return;
        LOGD("moneyReceived %" PRIu64, amount);
    }

    void unconfirmedMoneyReceived(const std::string &txId, uint64_t amount) {
        std::lock_guard<std::mutex> lock(_listenerMutex);
        if (jlistener == nullptr) return;
        LOGD("unconfirmedMoneyReceived %" PRIu64, amount);
    }

    void newBlock(uint64_t height) {
        std::lock_guard<std::mutex> lock(_listenerMutex);
        if (jlistener == nullptr) return;
        JNIEnv *jenv;
        int envStat = attachJVM(&jenv);
        if (envStat == JNI_ERR) return;

        jlong h = static_cast<jlong>(height);
        jmethodID listenerClass_newBlock = jenv->GetMethodID(class_WalletListener, "newBlock", "(J)V");
        if (listenerClass_newBlock != nullptr) {
            jenv->CallVoidMethod(jlistener, listenerClass_newBlock, h);
        }

        detachJVM(jenv, envStat);
    }

    void refreshed() {
        std::lock_guard<std::mutex> lock(_listenerMutex);
        if (jlistener == nullptr) return;
        LOGD("refreshed");
        JNIEnv *jenv;

        int envStat = attachJVM(&jenv);
        if (envStat == JNI_ERR) return;

        jmethodID listenerClass_refreshed = jenv->GetMethodID(class_WalletListener, "refreshed", "()V");
        if (listenerClass_refreshed != nullptr) {
            jenv->CallVoidMethod(jlistener, listenerClass_refreshed);
        }
        detachJVM(jenv, envStat);
    }
};

/**********************************/
/********** WalletManager *********/
/**********************************/

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_createWalletJ(JNIEnv *env, jobject instance,
                                                            jstring path, jstring password,
                                                            jstring language, jint networkType) {
    if (path == nullptr || password == nullptr || language == nullptr) {
        return 0;
    }
    
    JNIStringGuard _path(env, path);
    JNIStringGuard _password(env, password);
    JNIStringGuard _language(env, language);
    
    if (!_path.isValid() || !_password.isValid() || !_language.isValid()) {
        return 0;
    }
    
    Monero::NetworkType _networkType = static_cast<Monero::NetworkType>(networkType);

    Monero::Wallet *wallet = Monero::WalletManagerFactory::getWalletManager()->createWallet(
            std::string(_path.get()), std::string(_password.get()), std::string(_language.get()), _networkType);

    
    logWalletCreation(wallet, "createWalletJ");  // ADDED: Log wallet creation
return reinterpret_cast<jlong>(wallet);
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_openWalletJ(JNIEnv *env, jobject instance,
                                                          jstring path, jstring password,
                                                          jint networkType) {
    if (path == nullptr || password == nullptr) {
        return 0;
    }
    
    JNIStringGuard _path(env, path);
    JNIStringGuard _password(env, password);
    
    if (!_path.isValid() || !_password.isValid()) {
        return 0;
    }
    
    Monero::NetworkType _networkType = static_cast<Monero::NetworkType>(networkType);

    Monero::Wallet *wallet = Monero::WalletManagerFactory::getWalletManager()->openWallet(
            std::string(_path.get()), std::string(_password.get()), _networkType);

    
    logWalletCreation(wallet, "openWalletJ");  // ADDED: Log wallet creation
return reinterpret_cast<jlong>(wallet);
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_recoveryWalletJ(JNIEnv *env, jobject instance,
                                                              jstring path, jstring password,
                                                              jstring mnemonic, jstring offset,
                                                              jint networkType, jlong restoreHeight) {
    if (path == nullptr || password == nullptr || mnemonic == nullptr || offset == nullptr) {
        return 0;
    }
    
    JNIStringGuard _path(env, path);
    JNIStringGuard _password(env, password);
    JNIStringGuard _mnemonic(env, mnemonic);
    JNIStringGuard _offset(env, offset);
    
    if (!_path.isValid() || !_password.isValid() || !_mnemonic.isValid() || !_offset.isValid()) {
        return 0;
    }
    
    Monero::NetworkType _networkType = static_cast<Monero::NetworkType>(networkType);

    Monero::Wallet *wallet = Monero::WalletManagerFactory::getWalletManager()->recoveryWallet(
            std::string(_path.get()), std::string(_password.get()), std::string(_mnemonic.get()), _networkType,
            (uint64_t) restoreHeight, 1, std::string(_offset.get()));

    
    logWalletCreation(wallet, "recoveryWalletJ");  // ADDED: Log wallet creation
return reinterpret_cast<jlong>(wallet);
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_createWalletFromKeysJ(JNIEnv *env, jobject instance,
                                                                    jstring path, jstring password,
                                                                    jstring language, jint networkType,
                                                                    jlong restoreHeight, jstring addressString,
                                                                    jstring viewKeyString, jstring spendKeyString) {
    if (path == nullptr || password == nullptr || language == nullptr || 
        addressString == nullptr || viewKeyString == nullptr || spendKeyString == nullptr) {
        return 0;
    }
    
    JNIStringGuard _path(env, path);
    JNIStringGuard _password(env, password);
    JNIStringGuard _language(env, language);
    JNIStringGuard _addressString(env, addressString);
    JNIStringGuard _viewKeyString(env, viewKeyString);
    JNIStringGuard _spendKeyString(env, spendKeyString);
    
    if (!_path.isValid() || !_password.isValid() || !_language.isValid() ||
        !_addressString.isValid() || !_viewKeyString.isValid() || !_spendKeyString.isValid()) {
        return 0;
    }
    
    Monero::NetworkType _networkType = static_cast<Monero::NetworkType>(networkType);

    Monero::Wallet *wallet = Monero::WalletManagerFactory::getWalletManager()->createWalletFromKeys(
            std::string(_path.get()), std::string(_password.get()), std::string(_language.get()), _networkType,
            (uint64_t) restoreHeight, std::string(_addressString.get()), std::string(_viewKeyString.get()),
            std::string(_spendKeyString.get()));

    
    logWalletCreation(wallet, "createWalletFromKeysJ");  // ADDED: Log wallet creation
return reinterpret_cast<jlong>(wallet);
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_createWalletFromDeviceJ(JNIEnv *env, jobject instance,
                                                                      jstring path, jstring password,
                                                                      jstring deviceName, jint networkType,
                                                                      jlong restoreHeight, jstring subaddressLookahead) {
    if (path == nullptr || password == nullptr || deviceName == nullptr || subaddressLookahead == nullptr) {
        return 0;
    }
    
    JNIStringGuard _path(env, path);
    JNIStringGuard _password(env, password);
    JNIStringGuard _deviceName(env, deviceName);
    JNIStringGuard _subaddressLookahead(env, subaddressLookahead);
    
    if (!_path.isValid() || !_password.isValid() || !_deviceName.isValid() || !_subaddressLookahead.isValid()) {
        return 0;
    }
    
    Monero::NetworkType _networkType = static_cast<Monero::NetworkType>(networkType);

    Monero::Wallet *wallet = Monero::WalletManagerFactory::getWalletManager()->createWalletFromDevice(
            std::string(_path.get()), std::string(_password.get()), _networkType, std::string(_deviceName.get()),
            (uint64_t) restoreHeight, std::string(_subaddressLookahead.get()));

    
    logWalletCreation(wallet, "createWalletFromDeviceJ");  // ADDED: Log wallet creation
return reinterpret_cast<jlong>(wallet);
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_walletExists(JNIEnv *env, jobject instance, jstring path) {
    if (path == nullptr) {
        return JNI_FALSE;
    }
    
    JNIStringGuard _path(env, path);
    if (!_path.isValid()) {
        return JNI_FALSE;
    }
    
    bool exists = Monero::WalletManagerFactory::getWalletManager()->walletExists(std::string(_path.get()));
    return static_cast<jboolean>(exists);
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_verifyWalletPassword(JNIEnv *env, jobject instance,
                                                                   jstring keys_file_name, jstring password,
                                                                   jboolean watch_only) {
    if (keys_file_name == nullptr || password == nullptr) {
        return JNI_FALSE;
    }
    
    JNIStringGuard _keys_file_name(env, keys_file_name);
    JNIStringGuard _password(env, password);
    
    if (!_keys_file_name.isValid() || !_password.isValid()) {
        return JNI_FALSE;
    }
    
    bool passwordOk = Monero::WalletManagerFactory::getWalletManager()->verifyWalletPassword(
            std::string(_keys_file_name.get()), std::string(_password.get()), watch_only);
    return static_cast<jboolean>(passwordOk);
}

JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_queryWalletDeviceJ(JNIEnv *env, jobject instance,
                                                                 jstring keys_file_name, jstring password) {
    if (keys_file_name == nullptr || password == nullptr) {
        return WALLET_DEVICE_QUERY_ERROR;
    }
    
    JNIStringGuard _keys_file_name(env, keys_file_name);
    JNIStringGuard _password(env, password);
    
    if (!_keys_file_name.isValid() || !_password.isValid()) {
        return WALLET_DEVICE_QUERY_ERROR;
    }
    
    Monero::Wallet::Device device_type;
    bool ok = Monero::WalletManagerFactory::getWalletManager()->queryWalletDevice(
            device_type, std::string(_keys_file_name.get()), std::string(_password.get()));
    
    if (ok)
        return static_cast<jint>(device_type);
    else
        return WALLET_DEVICE_QUERY_ERROR;
}

JNIEXPORT jobject JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_findWallets(JNIEnv *env, jobject instance, jstring path) {
    if (path == nullptr) {
        return cpp2java(env, {});
    }
    
    JNIStringGuard _path(env, path);
    if (!_path.isValid()) {
        return cpp2java(env, {});
    }
    
    std::vector<std::string> walletPaths = Monero::WalletManagerFactory::getWalletManager()->findWallets(std::string(_path.get()));
    return cpp2java(env, walletPaths);
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_setDaemonAddressJ(JNIEnv *env, jobject instance, jstring address) {
    if (address == nullptr) {
        return;
    }
    
    JNIStringGuard _address(env, address);
    if (_address.isValid()) {
        Monero::WalletManagerFactory::getWalletManager()->setDaemonAddress(std::string(_address.get()));
    }
}

JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_getDaemonVersion(JNIEnv *env, jobject instance) {
    uint32_t version;
    bool isConnected = Monero::WalletManagerFactory::getWalletManager()->connected(&version);
    if (!isConnected) version = 0;
    return version;
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_getBlockchainHeight(JNIEnv *env, jobject instance) {
    return Monero::WalletManagerFactory::getWalletManager()->blockchainHeight();
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_getBlockchainTargetHeight(JNIEnv *env, jobject instance) {
    return Monero::WalletManagerFactory::getWalletManager()->blockchainTargetHeight();
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_startMining(JNIEnv *env, jobject instance,
                                                          jstring address, jboolean background_mining,
                                                          jboolean ignore_battery) {
    if (address == nullptr) {
        return JNI_FALSE;
    }
    
    JNIStringGuard _address(env, address);
    if (!_address.isValid()) {
        return JNI_FALSE;
    }
    
    bool success = Monero::WalletManagerFactory::getWalletManager()->startMining(
            std::string(_address.get()), background_mining, ignore_battery);
    return static_cast<jboolean>(success);
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_stopMining(JNIEnv *env, jobject instance) {
    return static_cast<jboolean>(Monero::WalletManagerFactory::getWalletManager()->stopMining());
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_resolveOpenAlias(JNIEnv *env, jobject instance,
                                                               jstring address, jboolean dnssec_valid) {
    if (address == nullptr) {
        return env->NewStringUTF("");
    }
    
    JNIStringGuard _address(env, address);
    if (!_address.isValid()) {
        return env->NewStringUTF("");
    }
    
    bool _dnssec_valid = (bool) dnssec_valid;
    std::string resolvedAlias = Monero::WalletManagerFactory::getWalletManager()->resolveOpenAlias(
            std::string(_address.get()), _dnssec_valid);
    return env->NewStringUTF(resolvedAlias.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_setProxy(JNIEnv *env, jobject instance, jstring address) {
    if (address == nullptr) {
        return JNI_FALSE;
    }
    
    JNIStringGuard _address(env, address);
    if (!_address.isValid()) {
        return JNI_FALSE;
    }
    
    bool rc = Monero::WalletManagerFactory::getWalletManager()->setProxy(std::string(_address.get()));
    return rc;
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_closeJ(JNIEnv *env, jobject instance, jobject walletInstance) {
    LOGD("closeJ: ENTER - closing wallet");
    
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, walletInstance);
    if (wallet == nullptr) {
        LOGE("closeJ: ERROR - wallet handle is null");
        return JNI_FALSE;
    }
    
    LOGD("closeJ: wallet handle: %p", wallet);
    logAllWallets();  // Log before close
    
    bool closeSuccess = Monero::WalletManagerFactory::getWalletManager()->closeWallet(wallet, false);
    
    if (closeSuccess) {
        MyWalletListener *walletListener = getHandle<MyWalletListener>(env, walletInstance, "listenerHandle");
        if (walletListener != nullptr) {
            LOGD("closeJ: deleting wallet listener: %p", walletListener);
            walletListener->deleteGlobalJavaRef(env);
            delete walletListener;
        }
        
        // Log the deletion
        logWalletDeletion(wallet);
    }
    
    LOGD("closeJ: wallet closed %s, success: %d", 
         closeSuccess ? "successfully" : "failed", closeSuccess);
    
    return static_cast<jboolean>(closeSuccess);
}

/**********************************/
/************ Wallet **************/
/**********************************/

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_rescanSpent(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet != nullptr) {
        wallet->rescanSpent();
    }
}

static std::mutex g_walletMutex;

static jstring emptyString(JNIEnv* env) {
    return env->NewStringUTF("");
}

static jstring getSeedInternal(JNIEnv *env, jobject instance, const std::string &offset) {
    LOGD("getSeedInternal: ENTER - thread: %ld, offset: '%s'", 
         (long)gettid(), offset.c_str());
    
    if (env == nullptr) {
        LOGE("getSeedInternal: ERROR - JNIEnv is null");
        return nullptr;
    }
    
    // Clear any pending exceptions at entry
    if (env->ExceptionCheck()) {
        LOGW("getSeedInternal: Clearing pre-existing exception at entry");
        env->ExceptionClear();
    }
    
    // Get wallet handle with safety check
    Monero::Wallet *wallet = nullptr;
    try {
        wallet = getHandle<Monero::Wallet>(env, instance);
    } catch (...) {
        LOGE("getSeedInternal: Exception in getHandle");
        wallet = nullptr;
    }
    
    if (!wallet) {
        LOGE("getSeedInternal: ERROR - Wallet handle is null");
        // Create empty string to return
        jstring emptyResult = env->NewStringUTF("");
        if (!emptyResult || env->ExceptionCheck()) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            return nullptr;
        }
        return emptyResult;
    }
    
    // ============================================================
    // CRITICAL SECTION - Get seed
    // ============================================================
    
    std::string seed;
    bool success = false;
    
    {
        std::lock_guard<std::mutex> lock(g_walletMutex);
        LOGD("getSeedInternal: acquired wallet mutex");
        
        // Clear exceptions before native code
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        
        // Wallet status check
        try {
            if (wallet->status() != Monero::Wallet::Status_Ok) {
                std::string err;
                int status;
                wallet->statusWithErrorString(status, err);
                LOGE("getSeedInternal: Wallet status not OK (%d): %s", status, err.c_str());
                // Don't return here - let cleanup handle it
            } else {
                // Get seed with the provided offset
                seed = wallet->seed(offset);
                LOGD("getSeedInternal: Successfully retrieved seed (length: %zu)", seed.length());
                success = true;
            }
        } catch (const std::exception& e) {
            LOGE("getSeedInternal: EXCEPTION in wallet->seed: %s", e.what());
        } catch (...) {
            LOGE("getSeedInternal: UNKNOWN EXCEPTION in wallet->seed");
        }
    } // Lock automatically released here
    
    // ============================================================
    // CREATE RETURN VALUE WITH IMPROVED ERROR HANDLING
    // ============================================================
    
    if (!success) {
        // Return empty string on failure
        jstring emptyResult = env->NewStringUTF("");
        if (!emptyResult || env->ExceptionCheck()) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            // Last resort - return null
            return nullptr;
        }
        return emptyResult;
    }
    
    // Clear exceptions before creating return value
    if (env->ExceptionCheck()) {
        LOGW("getSeedInternal: Clearing exception before NewStringUTF");
        env->ExceptionClear();
    }
    
    jstring result = nullptr;
    try {
        result = env->NewStringUTF(seed.c_str());
    } catch (...) {
        LOGE("getSeedInternal: Exception in NewStringUTF");
        result = nullptr;
    }
    
    if (result == nullptr) {
        LOGE("getSeedInternal: NewStringUTF returned null");
        
        // Try to return empty string as fallback
        jstring fallback = env->NewStringUTF("");
        if (!fallback || env->ExceptionCheck()) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            return nullptr;
        }
        return fallback;
    }
    
    // Check for exception after NewStringUTF
    if (env->ExceptionCheck()) {
        LOGE("getSeedInternal: Exception after NewStringUTF");
        env->ExceptionClear();
        
        // Clean up and return fallback
        env->DeleteLocalRef(result);
        jstring fallback = env->NewStringUTF("");
        if (!fallback || env->ExceptionCheck()) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            return nullptr;
        }
        return fallback;
    }
    
    LOGD("getSeedInternal: EXIT - success");
    return result;
}

// ============================================================
// SAFE STRING EXTRACTION HELPER
// ============================================================

static bool extractJString(JNIEnv *env, jstring jstr, std::string &result) {
    if (env == nullptr || jstr == nullptr) {
        return false;
    }
    
    // Clear any pending exceptions
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    
    const char *cstr = nullptr;
    jboolean isCopy = JNI_FALSE;
    
    try {
        cstr = env->GetStringUTFChars(jstr, &isCopy);
    } catch (...) {
        LOGE("extractJString: Exception in GetStringUTFChars");
        return false;
    }
    
    if (cstr == nullptr) {
        LOGE("extractJString: GetStringUTFChars returned null");
        return false;
    }
    
    if (env->ExceptionCheck()) {
        LOGE("extractJString: Exception after GetStringUTFChars");
        env->ExceptionClear();
        
        // Still need to release the string if we got it
        try {
            env->ReleaseStringUTFChars(jstr, cstr);
        } catch (...) {
            // Ignore release errors
        }
        return false;
    }
    
    // Copy the string
    try {
        result.assign(cstr);
    } catch (const std::exception& e) {
        LOGE("extractJString: Exception copying string: %s", e.what());
        try {
            env->ReleaseStringUTFChars(jstr, cstr);
        } catch (...) {
            // Ignore release errors
        }
        return false;
    } catch (...) {
        LOGE("extractJString: Unknown exception copying string");
        try {
            env->ReleaseStringUTFChars(jstr, cstr);
        } catch (...) {
            // Ignore release errors
        }
        return false;
    }
    
    // Release the string
    try {
        env->ReleaseStringUTFChars(jstr, cstr);
    } catch (const std::exception& e) {
        LOGE("extractJString: Exception releasing string: %s", e.what());
        // Still return success since we got the string
    } catch (...) {
        LOGE("extractJString: Unknown exception releasing string");
        // Still return success since we got the string
    }
    
    return true;
}

// ============================================================
// JNI FUNCTION 1: getSeed() - NO PARAMETERS
// ============================================================
// Maps to: public native String getSeed();
// JNI name mangling: Double underscore (__) for overloaded method with no params

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getSeed__(
        JNIEnv *env,
        jobject instance) {
    
    LOGD("Java_Wallet_getSeed__: ENTER (no parameters)");
    
    // Clear any pending exceptions
    if (env->ExceptionCheck()) {
        LOGW("Java_Wallet_getSeed__: Clearing pre-existing exception");
        env->ExceptionClear();
    }
    
    // Call internal implementation with empty offset (standard seed)
    jstring result = getSeedInternal(env, instance, "");
    
    LOGD("Java_Wallet_getSeed__: EXIT %s", 
         result != nullptr ? "success" : "failed");
    return result;
}

// ============================================================
// JNI FUNCTION 2: getSeed(String) - WITH OFFSET PARAMETER
// ============================================================
// Maps to: public native String getSeed(String seedOffset);
// JNI name mangling: __Ljava_lang_String_2 for (String) parameter

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getSeed__Ljava_lang_String_2(
        JNIEnv *env,
        jobject instance,
        jstring seedOffset) {
    
    LOGD("Java_Wallet_getSeed__Ljava_lang_String_2: ENTER (with offset parameter)");
    
    // Clear any pre-existing exceptions
    if (env->ExceptionCheck()) {
        LOGW("Java_Wallet_getSeed__Ljava_lang_String_2: Clearing pre-existing exception");
        env->ExceptionClear();
    }
    
    // ============================================================
    // SAFELY EXTRACT OFFSET STRING
    // ============================================================
    
    std::string offset;
    
    if (seedOffset != nullptr) {
        // Use the safe extraction helper
        if (!extractJString(env, seedOffset, offset)) {
            LOGW("Java_Wallet_getSeed__Ljava_lang_String_2: Failed to extract offset, using empty");
            offset.clear();
        } else {
            LOGD("Java_Wallet_getSeed__Ljava_lang_String_2: Extracted offset '%s' (length: %zu)", 
                 offset.c_str(), offset.length());
        }
    } else {
        LOGD("Java_Wallet_getSeed__Ljava_lang_String_2: seedOffset is null, using empty offset");
    }
    
    // Call internal implementation with extracted offset
    jstring result = getSeedInternal(env, instance, offset);
    
    LOGD("Java_Wallet_getSeed__Ljava_lang_String_2: EXIT %s", 
         result != nullptr ? "success" : "failed");
    return result;
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getSeedLanguage(
        JNIEnv *env,
        jobject instance) {

    std::lock_guard<std::mutex> lock(g_walletMutex);

    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (!wallet || wallet->status() != Monero::Wallet::Status_Ok) {
        return emptyString(env);
    }

    std::string language = wallet->getSeedLanguage();
    return env->NewStringUTF(language.c_str());
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_setSeedLanguage(
        JNIEnv *env,
        jobject instance,
        jstring language) {

    if (language == nullptr) return;

    std::lock_guard<std::mutex> lock(g_walletMutex);

    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (!wallet || wallet->status() != Monero::Wallet::Status_Ok) {
        return;
    }

    JNIStringGuard langGuard(env, language);
    if (langGuard.isValid()) {
        wallet->setSeedLanguage(std::string(langGuard.get()));
    }
}

JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getStatusJ(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->status();
}

JNIEXPORT jobject JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_statusWithErrorString(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return newWalletStatusInstance(env, 0, "Wallet not initialized");
    }

    int status;
    std::string errorString;
    wallet->statusWithErrorString(status, errorString);

    return newWalletStatusInstance(env, status, errorString);
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_setPassword(JNIEnv *env, jobject instance, jstring password) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || password == nullptr) {
        return JNI_FALSE;
    }
    
    JNIStringGuard _password(env, password);
    if (!_password.isValid()) {
        return JNI_FALSE;
    }
    
    bool success = wallet->setPassword(std::string(_password.get()));
    return static_cast<jboolean>(success);
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getAddressJ(JNIEnv *env, jobject instance,
                                                   jint accountIndex, jint addressIndex) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return env->NewStringUTF("");
    }
    std::string address = wallet->address((uint32_t) accountIndex, (uint32_t) addressIndex);
    return env->NewStringUTF(address.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getPath(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return env->NewStringUTF("");
    }
    std::string path = wallet->path();
    return env->NewStringUTF(path.c_str());
}

JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_nettype(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->nettype();
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getIntegratedAddress(JNIEnv *env, jobject instance, jstring payment_id) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || payment_id == nullptr) {
        return env->NewStringUTF("");
    }
    
    JNIStringGuard _payment_id(env, payment_id);
    std::string address = _payment_id.isValid() ? 
        wallet->integratedAddress(_payment_id.get()) : 
        "";
    return env->NewStringUTF(address.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getSecretViewKey(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return env->NewStringUTF("");
    }
    std::string viewKey = wallet->secretViewKey();
    return env->NewStringUTF(viewKey.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getSecretSpendKey(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return env->NewStringUTF("");
    }
    std::string spendKey = wallet->secretSpendKey();
    return env->NewStringUTF(spendKey.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_store(JNIEnv *env, jobject instance, jstring path) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || path == nullptr) {
        return JNI_FALSE;
    }
    
    JNIStringGuard _path(env, path);
    if (!_path.isValid()) {
        return JNI_FALSE;
    }
    
    bool success = wallet->store(std::string(_path.get()));
    if (!success) {
        LOGE("store() %s", wallet->errorString().c_str());
    }
    return static_cast<jboolean>(success);
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getFilename(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return env->NewStringUTF("");
    }
    std::string filename = wallet->filename();
    return env->NewStringUTF(filename.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_initJ(JNIEnv *env, jobject instance,
                                             jstring daemon_address, jlong upper_transaction_size_limit,
                                             jstring daemon_username, jstring daemon_password) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || daemon_address == nullptr) {
        return JNI_FALSE;
    }
    
    JNIStringGuard _daemon_address(env, daemon_address);
    JNIStringGuard _daemon_username(env, daemon_username);
    JNIStringGuard _daemon_password(env, daemon_password);
    
    if (!_daemon_address.isValid()) {
        return JNI_FALSE;
    }
    
    const char* username = _daemon_username.isValid() ? _daemon_username.get() : "";
    const char* password = _daemon_password.isValid() ? _daemon_password.get() : "";
    
    bool status = wallet->init(_daemon_address.get(), (uint64_t) upper_transaction_size_limit,
                               username, password);
    return static_cast<jboolean>(status);
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_setRestoreHeight(JNIEnv *env, jobject instance, jlong height) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet != nullptr) {
        wallet->setRefreshFromBlockHeight((uint64_t) height);
    }
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getRestoreHeight(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->getRefreshFromBlockHeight();
}

JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getConnectionStatusJ(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->connected();
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_setProxy(JNIEnv *env, jobject instance, jstring address) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || address == nullptr) {
        return JNI_FALSE;
    }
    
    JNIStringGuard _address(env, address);
    if (!_address.isValid()) {
        return JNI_FALSE;
    }
    
    bool rc = wallet->setProxy(std::string(_address.get()));
    return rc;
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getBalance(JNIEnv *env, jobject instance, jint accountIndex) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->balance((uint32_t) accountIndex);
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getBalanceAll(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->balanceAll();
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getUnlockedBalance(JNIEnv *env, jobject instance, jint accountIndex) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->unlockedBalance((uint32_t) accountIndex);
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getUnlockedBalanceAll(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->unlockedBalanceAll();
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_isWatchOnly(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return JNI_FALSE;
    }
    return static_cast<jboolean>(wallet->watchOnly());
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getBlockChainHeight(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->blockChainHeight();
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getApproximateBlockChainHeight(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->approximateBlockChainHeight();
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getDaemonBlockChainHeight(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->daemonBlockChainHeight();
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getDaemonBlockChainTargetHeight(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->daemonBlockChainTargetHeight();
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_isSynchronizedJ(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return JNI_FALSE;
    }
    return static_cast<jboolean>(wallet->synchronized());
}

JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getDeviceTypeJ(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    Monero::Wallet::Device device_type = wallet->getDeviceType();
    return static_cast<jint>(device_type);
}

JNIEXPORT jbyteArray JNICALL
Java_com_m2049r_xmrwallet_util_KeyStoreHelper_slowHash(JNIEnv *env, jclass clazz,
                                                       jbyteArray data, jint brokenVariant) {
    if (data == nullptr) {
        return nullptr;
    }
    
    char hash[HASH_SIZE];
    jsize size = env->GetArrayLength(data);
    if ((brokenVariant > 0) && (size < 200)) {
        return nullptr;
    }

    jbyte *buffer = env->GetByteArrayElements(data, nullptr);
    if (buffer == nullptr) {
        return nullptr;
    }
    
    switch (brokenVariant) {
        case 1:
            slow_hash_broken(buffer, hash, 1);
            break;
        case 2:
            slow_hash_broken(buffer, hash, 0);
            break;
        default:
            slow_hash(buffer, (size_t) size, hash);
    }
    env->ReleaseByteArrayElements(data, buffer, JNI_ABORT);
    jbyteArray result = env->NewByteArray(HASH_SIZE);
    if (result != nullptr) {
        env->SetByteArrayRegion(result, 0, HASH_SIZE, (jbyte *) hash);
    }
    return result;
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getDisplayAmount(JNIEnv *env, jclass clazz, jlong amount) {
    std::string displayAmount = Monero::Wallet::displayAmount(amount);
    return env->NewStringUTF(displayAmount.c_str());
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getAmountFromString(JNIEnv *env, jclass clazz, jstring amount) {
    if (amount == nullptr) {
        return 0;
    }
    
    JNIStringGuard _amount(env, amount);
    if (!_amount.isValid()) {
        return 0;
    }
    
    uint64_t x = Monero::Wallet::amountFromString(_amount.get());
    return x;
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getAmountFromDouble(JNIEnv *env, jclass clazz, jdouble amount) {
    return Monero::Wallet::amountFromDouble(amount);
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_generatePaymentId(JNIEnv *env, jclass clazz) {
    std::string paymentId = Monero::Wallet::genPaymentId();
    return env->NewStringUTF(paymentId.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_isPaymentIdValid(JNIEnv *env, jclass clazz, jstring payment_id) {
    if (payment_id == nullptr) {
        return JNI_FALSE;
    }
    
    JNIStringGuard _payment_id(env, payment_id);
    if (!_payment_id.isValid()) {
        return JNI_FALSE;
    }
    
    bool isValid = Monero::Wallet::paymentIdValid(_payment_id.get());
    return static_cast<jboolean>(isValid);
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_isAddressValid(JNIEnv *env, jclass clazz,
                                                      jstring address, jint networkType) {
    if (address == nullptr) {
        return JNI_FALSE;
    }
    
    JNIStringGuard _address(env, address);
    if (!_address.isValid()) {
        return JNI_FALSE;
    }
    
    Monero::NetworkType _networkType = static_cast<Monero::NetworkType>(networkType);
    bool isValid = Monero::Wallet::addressValid(_address.get(), _networkType);
    return static_cast<jboolean>(isValid);
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getPaymentIdFromAddress(JNIEnv *env, jclass clazz,
                                                               jstring address, jint networkType) {
    if (address == nullptr) {
        return env->NewStringUTF("");
    }
    
    JNIStringGuard _address(env, address);
    if (!_address.isValid()) {
        return env->NewStringUTF("");
    }
    
    Monero::NetworkType _networkType = static_cast<Monero::NetworkType>(networkType);
    std::string payment_id = Monero::Wallet::paymentIdFromAddress(_address.get(), _networkType);
    return env->NewStringUTF(payment_id.c_str());
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getMaximumAllowedAmount(JNIEnv *env, jclass clazz) {
    return Monero::Wallet::maximumAllowedAmount();
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_startRefresh(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet != nullptr) {
        wallet->startRefresh();
    }
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_pauseRefresh(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet != nullptr) {
        wallet->pauseRefresh();
    }
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_refresh(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return JNI_FALSE;
    }
    return static_cast<jboolean>(wallet->refresh());
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_refreshAsync(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet != nullptr) {
        wallet->refreshAsync();
    }
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_rescanBlockchainAsyncJ(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet != nullptr) {
        wallet->rescanBlockchainAsync();
    }
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_createTransactionMultDest(JNIEnv *env, jobject instance,
                                                                 jobjectArray destinations, jstring payment_id,
                                                                 jlongArray amounts, jint mixin_count,
                                                                 jint priority, jint accountIndex,
                                                                 jintArray subaddresses) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || destinations == nullptr || amounts == nullptr) {
        return 0;
    }
    
    int destSize = env->GetArrayLength(destinations);
    int amountSize = env->GetArrayLength(amounts);
    
    if (destSize != amountSize || destSize == 0) {
        return 0;
    }

    std::vector<std::string> dst_addr;
    std::vector<uint64_t> amount;

    jlong *_amounts = env->GetLongArrayElements(amounts, nullptr);
    if (_amounts == nullptr) {
        return 0;
    }
    
    for (int i = 0; i < destSize; i++) {
        jstring dest = (jstring) env->GetObjectArrayElement(destinations, i);
        if (dest == nullptr) {
            env->ReleaseLongArrayElements(amounts, _amounts, 0);
            return 0;
        }
        
        JNIStringGuard destGuard(env, dest);
        if (!destGuard.isValid()) {
            env->ReleaseLongArrayElements(amounts, _amounts, 0);
            env->DeleteLocalRef(dest);
            return 0;
        }
        
        dst_addr.emplace_back(destGuard.get());
        amount.emplace_back((uint64_t) _amounts[i]);
        env->DeleteLocalRef(dest);
    }
    env->ReleaseLongArrayElements(amounts, _amounts, 0);

    std::set<uint32_t> subaddr_indices;
    if (subaddresses != nullptr) {
        int subaddrSize = env->GetArrayLength(subaddresses);
        jint *_subaddresses = env->GetIntArrayElements(subaddresses, nullptr);
        if (_subaddresses != nullptr) {
            for (int i = 0; i < subaddrSize; i++) {
                subaddr_indices.insert((uint32_t) _subaddresses[i]);
            }
            env->ReleaseIntArrayElements(subaddresses, _subaddresses, 0);
        }
    }

    JNIStringGuard paymentIdGuard(env, payment_id);
    const char* payment_id_str = paymentIdGuard.isValid() ? paymentIdGuard.get() : "";

    Monero::PendingTransaction::Priority _priority = static_cast<Monero::PendingTransaction::Priority>(priority);

    Monero::PendingTransaction *tx = wallet->createTransactionMultDest(dst_addr, payment_id_str, amount,
                                                                       (uint32_t) mixin_count, _priority,
                                                                       (uint32_t) accountIndex, subaddr_indices);

    return reinterpret_cast<jlong>(tx);
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_createTransactionJ(JNIEnv *env, jobject instance,
                                                          jstring dst_addr, jstring payment_id,
                                                          jlong amount, jint mixin_count,
                                                          jint priority, jint accountIndex) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || dst_addr == nullptr) {
        return 0;
    }
    
    JNIStringGuard _dst_addr(env, dst_addr);
    JNIStringGuard _payment_id(env, payment_id);
    
    if (!_dst_addr.isValid()) {
        return 0;
    }
    
    const char* payment_id_str = _payment_id.isValid() ? _payment_id.get() : "";
    Monero::PendingTransaction::Priority _priority = static_cast<Monero::PendingTransaction::Priority>(priority);

    Monero::PendingTransaction *tx = wallet->createTransaction(_dst_addr.get(), payment_id_str, amount,
                                                               (uint32_t) mixin_count, _priority, (uint32_t) accountIndex);

    return reinterpret_cast<jlong>(tx);
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_createSweepTransaction(JNIEnv *env, jobject instance,
                                                              jstring dst_addr, jstring payment_id,
                                                              jint mixin_count, jint priority, jint accountIndex) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || dst_addr == nullptr) {
        return 0;
    }
    
    JNIStringGuard _dst_addr(env, dst_addr);
    JNIStringGuard _payment_id(env, payment_id);
    
    if (!_dst_addr.isValid()) {
        return 0;
    }
    
    const char* payment_id_str = _payment_id.isValid() ? _payment_id.get() : "";
    Monero::PendingTransaction::Priority _priority = static_cast<Monero::PendingTransaction::Priority>(priority);

    // Use Monero's optional instead of std::optional
    Monero::optional<uint64_t> empty;
    Monero::PendingTransaction *tx = wallet->createTransaction(_dst_addr.get(), payment_id_str, empty,
                                                               (uint32_t) mixin_count, _priority, (uint32_t) accountIndex);

    return reinterpret_cast<jlong>(tx);
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_createSweepUnmixableTransactionJ(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    
    Monero::PendingTransaction *tx = wallet->createSweepUnmixableTransaction();
    return reinterpret_cast<jlong>(tx);
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_disposeTransaction(JNIEnv *env, jobject instance, jobject pendingTransaction) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    Monero::PendingTransaction *_pendingTransaction = getHandle<Monero::PendingTransaction>(env, pendingTransaction);
    if (wallet != nullptr && _pendingTransaction != nullptr) {
        wallet->disposeTransaction(_pendingTransaction);
    }
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_estimateTransactionFee(JNIEnv *env, jobject instance,
                                                              jobjectArray addresses, jlongArray amounts, jint priority) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || addresses == nullptr || amounts == nullptr) {
        return 0;
    }
    
    int destSize = env->GetArrayLength(addresses);
    int amountSize = env->GetArrayLength(amounts);
    
    if (destSize != amountSize || destSize == 0) {
        return 0;
    }

    std::vector<std::pair<std::string, uint64_t>> destinations;

    jlong *_amounts = env->GetLongArrayElements(amounts, nullptr);
    if (_amounts == nullptr) {
        return 0;
    }
    
    for (int i = 0; i < destSize; i++) {
        std::pair<std::string, uint64_t> pair;
        jstring dest = (jstring) env->GetObjectArrayElement(addresses, i);
        if (dest == nullptr) {
            env->ReleaseLongArrayElements(amounts, _amounts, 0);
            return 0;
        }
        
        JNIStringGuard destGuard(env, dest);
        if (!destGuard.isValid()) {
            env->ReleaseLongArrayElements(amounts, _amounts, 0);
            env->DeleteLocalRef(dest);
            return 0;
        }
        
        pair.first = destGuard.get();
        pair.second = ((uint64_t) _amounts[i]);
        destinations.emplace_back(pair);
        env->DeleteLocalRef(dest);
    }
    env->ReleaseLongArrayElements(amounts, _amounts, 0);

    Monero::PendingTransaction::Priority _priority = static_cast<Monero::PendingTransaction::Priority>(priority);

    return static_cast<jlong>(wallet->estimateTransactionFee(destinations, _priority));
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getHistoryJ(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return reinterpret_cast<jlong>(wallet->history());
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_setListenerJ(JNIEnv *env, jobject instance, jobject javaListener) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    
    wallet->setListener(nullptr);
    MyWalletListener *oldListener = getHandle<MyWalletListener>(env, instance, "listenerHandle");
    if (oldListener != nullptr) {
        oldListener->deleteGlobalJavaRef(env);
        delete oldListener;
    }
    if (javaListener == nullptr) {
        LOGD("null listener");
        return 0;
    } else {
        MyWalletListener *listener = new MyWalletListener(env, javaListener);
        wallet->setListener(listener);
        return reinterpret_cast<jlong>(listener);
    }
}

JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getDefaultMixin(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return wallet->defaultMixin();
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_setDefaultMixin(JNIEnv *env, jobject instance, jint mixin) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet != nullptr) {
        wallet->setDefaultMixin(mixin);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_setUserNote(JNIEnv *env, jobject instance, jstring txid, jstring note) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || txid == nullptr || note == nullptr) {
        return JNI_FALSE;
    }
    
    JNIStringGuard _txid(env, txid);
    JNIStringGuard _note(env, note);
    
    if (!_txid.isValid() || !_note.isValid()) {
        return JNI_FALSE;
    }
    
    bool success = wallet->setUserNote(_txid.get(), _note.get());
    return static_cast<jboolean>(success);
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getUserNote(JNIEnv *env, jobject instance, jstring txid) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || txid == nullptr) {
        return env->NewStringUTF("");
    }
    
    JNIStringGuard _txid(env, txid);
    if (!_txid.isValid()) {
        return env->NewStringUTF("");
    }
    
    std::string note = wallet->getUserNote(_txid.get());
    return env->NewStringUTF(note.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getTxKey(JNIEnv *env, jobject instance, jstring txid) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || txid == nullptr) {
        return env->NewStringUTF("");
    }
    
    JNIStringGuard _txid(env, txid);
    if (!_txid.isValid()) {
        return env->NewStringUTF("");
    }
    
    std::string txKey = wallet->getTxKey(_txid.get());
    return env->NewStringUTF(txKey.c_str());
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_addAccount(JNIEnv *env, jobject instance, jstring label) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || label == nullptr) {
        return;
    }
    
    JNIStringGuard _label(env, label);
    if (_label.isValid()) {
        wallet->addSubaddressAccount(_label.get());
    }
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getSubaddressLabel(JNIEnv *env, jobject instance,
                                                          jint accountIndex, jint addressIndex) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return env->NewStringUTF("");
    }
    std::string label = wallet->getSubaddressLabel((uint32_t) accountIndex, (uint32_t) addressIndex);
    return env->NewStringUTF(label.c_str());
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_setSubaddressLabel(JNIEnv *env, jobject instance,
                                                          jint accountIndex, jint addressIndex, jstring label) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || label == nullptr) {
        return;
    }
    
    JNIStringGuard _label(env, label);
    if (_label.isValid()) {
        wallet->setSubaddressLabel(accountIndex, addressIndex, _label.get());
    }
}

JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getNumAccounts(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return static_cast<jint>(wallet->numSubaddressAccounts());
}

JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getNumSubaddresses(JNIEnv *env, jobject instance, jint accountIndex) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return static_cast<jint>(wallet->numSubaddresses(accountIndex));
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_addSubaddress(JNIEnv *env, jobject instance,
                                                     jint accountIndex, jstring label) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || label == nullptr) {
        return;
    }
    
    JNIStringGuard _label(env, label);
    if (_label.isValid()) {
        wallet->addSubaddress(accountIndex, _label.get());
    }
}

/**********************************/
/********* Multisig Support *******/
/**********************************/

// Check if wallet is multisig
JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_isMultisig(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return JNI_FALSE;
    }
    return static_cast<jboolean>(wallet->multisig().isMultisig);
}

// Get multisig info string for wallet setup
JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getMultisigInfo(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return env->NewStringUTF("");
    }
    std::string multisigInfo = wallet->getMultisigInfo();
    return env->NewStringUTF(multisigInfo.c_str());
}

// Make multisig wallet from collected info
JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_makeMultisig(JNIEnv *env, jobject instance,
                                                     jobject multisigInfoList, jint threshold) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || multisigInfoList == nullptr) {
        return env->NewStringUTF("");
    }
    
    std::vector<std::string> info = java2cpp(env, multisigInfoList);
    if (info.empty()) {
        return env->NewStringUTF("");
    }
    
    std::string result = wallet->makeMultisig(info, (uint32_t) threshold);
    return env->NewStringUTF(result.c_str());
}

// Exchange multisig keys for M/N wallets (additional rounds for complex schemes)
JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_exchangeMultisigKeys(JNIEnv *env, jobject instance,
                                                            jobject multisigInfoList,
                                                            jboolean forceUpdateUseWithCaution) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || multisigInfoList == nullptr) {
        return env->NewStringUTF("");
    }
    
    std::vector<std::string> info = java2cpp(env, multisigInfoList);
    if (info.empty()) {
        return env->NewStringUTF("");
    }
    
    std::string result = wallet->exchangeMultisigKeys(info, forceUpdateUseWithCaution);
    return env->NewStringUTF(result.c_str());
}

// Export multisig images (key images) for wallet synchronization
// Note: exportMultisigImages takes a reference parameter and returns bool
JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_exportMultisigImages(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return env->NewStringUTF("");
    }
    
    std::string images;
    bool success = wallet->exportMultisigImages(images);
    if (!success) {
        LOGE("exportMultisigImages failed: %s", wallet->errorString().c_str());
        return env->NewStringUTF("");
    }
    
    return env->NewStringUTF(images.c_str());
}

// Import multisig images from other participants for wallet synchronization
JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_importMultisigImages(JNIEnv *env, jobject instance,
                                                            jobject multisigImagesList) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || multisigImagesList == nullptr) {
        return 0;
    }
    
    std::vector<std::string> images = java2cpp(env, multisigImagesList);
    if (images.empty()) {
        return 0;
    }
    
    size_t imported = wallet->importMultisigImages(images);
    return static_cast<jint>(imported);
}

// Restore a multisig transaction from exported data
// Returns a PendingTransaction handle that can be signed
JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_restoreMultisigTransaction(JNIEnv *env, jobject instance,
                                                                  jstring txData) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr || txData == nullptr) {
        return 0;
    }
    
    JNIStringGuard _txData(env, txData);
    if (!_txData.isValid()) {
        return 0;
    }
    
    Monero::PendingTransaction *tx = wallet->restoreMultisigTransaction(_txData.get());
    return reinterpret_cast<jlong>(tx);
}

// Get multisig state information as a structured object
JNIEXPORT jobject JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_getMultisigState(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return nullptr;
    }
    
    Monero::MultisigState state = wallet->multisig();
    
    // Create a Java object to return the multisig state
    // Requires a corresponding Java class: com.m2049r.xmrwallet.model.MultisigState
    // Constructor signature: (ZIII)V for (boolean isMultisig, int isReady, int threshold, int total)
    jclass multisigStateClass = env->FindClass("com/m2049r/xmrwallet/model/MultisigState");
    if (multisigStateClass == nullptr) {
        return nullptr;
    }
    
    jmethodID constructor = env->GetMethodID(multisigStateClass, "<init>", "(ZIII)V");
    if (constructor == nullptr) {
        return nullptr;
    }
    
    jobject stateObject = env->NewObject(multisigStateClass, constructor,
                                        static_cast<jboolean>(state.isMultisig),
                                        static_cast<jint>(state.isReady),
                                        static_cast<jint>(state.threshold),
                                        static_cast<jint>(state.total));
    
    return stateObject;
}

// Get the number of required signatures (threshold) for the multisig wallet
JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_multisigThreshold(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return static_cast<jint>(wallet->multisig().threshold);
}

// Check if multisig wallet is ready for creating/signing transactions
JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_isMultisigReady(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return JNI_FALSE;
    }
    return static_cast<jboolean>(wallet->multisig().isReady);
}

// Get total number of multisig participants
JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_Wallet_multisigTotal(JNIEnv *env, jobject instance) {
    Monero::Wallet *wallet = getHandle<Monero::Wallet>(env, instance);
    if (wallet == nullptr) {
        return 0;
    }
    return static_cast<jint>(wallet->multisig().total);
}

/**********************************/
/** PendingTransaction Multisig ***/
/**********************************/

// Export multisig sign data from a pending transaction
// This data is shared with other signers so they can sign the transaction
JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_PendingTransaction_multisigSignData(JNIEnv *env, jobject instance) {
    Monero::PendingTransaction *tx = getHandle<Monero::PendingTransaction>(env, instance);
    if (tx == nullptr) {
        return env->NewStringUTF("");
    }
    
    std::string signData = tx->multisigSignData();
    return env->NewStringUTF(signData.c_str());
}

// Sign a multisig transaction
// Note: This method returns void - it modifies the transaction in place
JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_PendingTransaction_signMultisigTx(JNIEnv *env, jobject instance) {
    Monero::PendingTransaction *tx = getHandle<Monero::PendingTransaction>(env, instance);
    if (tx != nullptr) {
        tx->signMultisigTx();
    }
}

// Get the list of public keys of signers who have already signed this transaction
JNIEXPORT jobject JNICALL
Java_com_m2049r_xmrwallet_model_PendingTransaction_getSignersKeys(JNIEnv *env, jobject instance) {
    Monero::PendingTransaction *tx = getHandle<Monero::PendingTransaction>(env, instance);
    if (tx == nullptr) {
        return cpp2java(env, std::vector<std::string>());
    }
    
    std::vector<std::string> signers = tx->signersKeys();
    return cpp2java(env, signers);
}

// TransactionHistory
JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_TransactionHistory_getCount(JNIEnv *env, jobject instance) {
    Monero::TransactionHistory *history = getHandle<Monero::TransactionHistory>(env, instance);
    if (history == nullptr) {
        return 0;
    }
    return history->count();
}

JNIEXPORT jobject JNICALL
Java_com_m2049r_xmrwallet_model_TransactionHistory_refreshJ(JNIEnv *env, jobject instance, jint accountIndex) {
    Monero::TransactionHistory *history = getHandle<Monero::TransactionHistory>(env, instance);
    if (history == nullptr) {
        return transactionInfoArrayList(env, {}, (uint32_t) accountIndex);
    }
    
    history->refresh();
    return transactionInfoArrayList(env, history->getAll(), (uint32_t) accountIndex);
}

// PendingTransaction
JNIEXPORT jint JNICALL
Java_com_m2049r_xmrwallet_model_PendingTransaction_getStatusJ(JNIEnv *env, jobject instance) {
    Monero::PendingTransaction *tx = getHandle<Monero::PendingTransaction>(env, instance);
    if (tx == nullptr) {
        return 0;
    }
    return tx->status();
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_PendingTransaction_getErrorString(JNIEnv *env, jobject instance) {
    Monero::PendingTransaction *tx = getHandle<Monero::PendingTransaction>(env, instance);
    if (tx == nullptr) {
        return env->NewStringUTF("");
    }
    std::string errorString = tx->errorString();
    return env->NewStringUTF(errorString.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_model_PendingTransaction_commit(JNIEnv *env, jobject instance,
                                                          jstring filename, jboolean overwrite) {
    Monero::PendingTransaction *tx = getHandle<Monero::PendingTransaction>(env, instance);
    if (tx == nullptr || filename == nullptr) {
        return JNI_FALSE;
    }
    
    JNIStringGuard _filename(env, filename);
    if (!_filename.isValid()) {
        return JNI_FALSE;
    }
    
    bool success = tx->commit(_filename.get(), overwrite);
    return static_cast<jboolean>(success);
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_PendingTransaction_getAmount(JNIEnv *env, jobject instance) {
    Monero::PendingTransaction *tx = getHandle<Monero::PendingTransaction>(env, instance);
    if (tx == nullptr) {
        return 0;
    }
    return static_cast<jlong>(tx->amount());
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_PendingTransaction_getDust(JNIEnv *env, jobject instance) {
    Monero::PendingTransaction *tx = getHandle<Monero::PendingTransaction>(env, instance);
    if (tx == nullptr) {
        return 0;
    }
    return static_cast<jlong>(tx->dust());
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_PendingTransaction_getFee(JNIEnv *env, jobject instance) {
    Monero::PendingTransaction *tx = getHandle<Monero::PendingTransaction>(env, instance);
    if (tx == nullptr) {
        return 0;
    }
    return static_cast<jlong>(tx->fee());
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_PendingTransaction_getFirstTxIdJ(JNIEnv *env, jobject instance) {
    Monero::PendingTransaction *tx = getHandle<Monero::PendingTransaction>(env, instance);
    if (tx == nullptr) {
        return env->NewStringUTF("");
    }
    
    std::vector<std::string> txids = tx->txid();
    if (!txids.empty())
        return env->NewStringUTF(txids.front().c_str());
    else
        return env->NewStringUTF("");
}

JNIEXPORT jlong JNICALL
Java_com_m2049r_xmrwallet_model_PendingTransaction_getTxCount(JNIEnv *env, jobject instance) {
    Monero::PendingTransaction *tx = getHandle<Monero::PendingTransaction>(env, instance);
    if (tx == nullptr) {
        return 0;
    }
    return static_cast<jlong>(tx->txCount());
}

// Logging
JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_initLogger(JNIEnv *env, jclass clazz,
                                                         jstring argv0, jstring default_log_base_name) {
    if (argv0 == nullptr || default_log_base_name == nullptr) {
        return;
    }
    
    JNIStringGuard _argv0(env, argv0);
    JNIStringGuard _default_log_base_name(env, default_log_base_name);
    
    if (_argv0.isValid() && _default_log_base_name.isValid()) {
        Monero::Wallet::init(_argv0.get(), _default_log_base_name.get());
    }
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_logDebug(JNIEnv *env, jclass clazz,
                                                       jstring category, jstring message) {
    if (category == nullptr || message == nullptr) {
        return;
    }
    
    JNIStringGuard _category(env, category);
    JNIStringGuard _message(env, message);
    
    if (_category.isValid() && _message.isValid()) {
        Monero::Wallet::debug(_category.get(), _message.get());
    }
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_logInfo(JNIEnv *env, jclass clazz,
                                                      jstring category, jstring message) {
    if (category == nullptr || message == nullptr) {
        return;
    }
    
    JNIStringGuard _category(env, category);
    JNIStringGuard _message(env, message);
    
    if (_category.isValid() && _message.isValid()) {
        Monero::Wallet::info(_category.get(), _message.get());
    }
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_logWarning(JNIEnv *env, jclass clazz,
                                                         jstring category, jstring message) {
    if (category == nullptr || message == nullptr) {
        return;
    }
    
    JNIStringGuard _category(env, category);
    JNIStringGuard _message(env, message);
    
    if (_category.isValid() && _message.isValid()) {
        Monero::Wallet::warning(_category.get(), _message.get());
    }
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_logError(JNIEnv *env, jclass clazz,
                                                       jstring category, jstring message) {
    if (category == nullptr || message == nullptr) {
        return;
    }
    
    JNIStringGuard _category(env, category);
    JNIStringGuard _message(env, message);
    
    if (_category.isValid() && _message.isValid()) {
        Monero::Wallet::error(_category.get(), _message.get());
    }
}

JNIEXPORT void JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_setLogLevel(JNIEnv *env, jclass clazz, jint level) {
    Monero::WalletManagerFactory::setLogLevel(level);
}

JNIEXPORT jstring JNICALL
Java_com_m2049r_xmrwallet_model_WalletManager_moneroVersion(JNIEnv *env, jclass clazz) {
    return env->NewStringUTF(MONERO_VERSION);
}

// Ledger
int LedgerExchange(unsigned char *command, unsigned int cmd_len,
                   unsigned char *response, unsigned int max_resp_len) {
    LOGD("LedgerExchange");
    JNIEnv *jenv;
    int envStat = attachJVM(&jenv);
    if (envStat == JNI_ERR) return -1;

    jmethodID exchangeMethod = jenv->GetStaticMethodID(class_Ledger, "Exchange", "([B)[B");
    if (exchangeMethod == nullptr) {
        detachJVM(jenv, envStat);
        return -1;
    }
    
    jsize sendLen = static_cast<jsize>(cmd_len);
    jbyteArray dataSend = jenv->NewByteArray(sendLen);
    if (dataSend == nullptr) {
        detachJVM(jenv, envStat);
        return -1;
    }
    
    jenv->SetByteArrayRegion(dataSend, 0, sendLen, (jbyte *) command);
    jbyteArray dataRecv = (jbyteArray) jenv->CallStaticObjectMethod(class_Ledger, exchangeMethod, dataSend);
    jenv->DeleteLocalRef(dataSend);
    if (dataRecv == nullptr) {
        detachJVM(jenv, envStat);
        LOGD("LedgerExchange SCARD_E_NO_READERS_AVAILABLE");
        return -1;
    }
    jsize len = jenv->GetArrayLength(dataRecv);
    LOGD("LedgerExchange SCARD_S_SUCCESS %u/%d", cmd_len, len);
    if (len <= max_resp_len) {
        jenv->GetByteArrayRegion(dataRecv, 0, len, (jbyte *) response);
        jenv->DeleteLocalRef(dataRecv);
        detachJVM(jenv, envStat);
        return static_cast<int>(len);
    } else {
        jenv->DeleteLocalRef(dataRecv);
        detachJVM(jenv, envStat);
        LOGE("LedgerExchange SCARD_E_INSUFFICIENT_BUFFER");
        return -1;
    }
}

int LedgerFind(char *buffer, size_t len) {
    LOGD("LedgerName");
    JNIEnv *jenv;
    int envStat = attachJVM(&jenv);
    if (envStat == JNI_ERR) return -2;

    jmethodID nameMethod = jenv->GetStaticMethodID(class_Ledger, "Name", "()Ljava/lang/String;");
    if (nameMethod == nullptr) {
        detachJVM(jenv, envStat);
        return -1;
    }
    
    jstring name = (jstring) jenv->CallStaticObjectMethod(class_Ledger, nameMethod);

    int ret;
    if (name != nullptr) {
        JNIStringGuard nameGuard(jenv, name);
        if (nameGuard.isValid()) {
            strncpy(buffer, nameGuard.get(), len);
            buffer[len - 1] = 0;
            ret = 0;
            LOGD("LedgerName is %s", buffer);
        } else {
            buffer[0] = 0;
            ret = -1;
        }
    } else {
        buffer[0] = 0;
        ret = -1;
    }

    detachJVM(jenv, envStat);
    return ret;
}

// Bluetooth/Sidekick
int BtExchange(unsigned char *request, unsigned int request_len,
               unsigned char *response, unsigned int max_resp_len) {
    JNIEnv *jenv;
    int envStat = attachJVM(&jenv);
    if (envStat == JNI_ERR) return -16;

    jmethodID exchangeMethod = jenv->GetStaticMethodID(class_BluetoothService, "Exchange", "([B)[B");
    if (exchangeMethod == nullptr) {
        detachJVM(jenv, envStat);
        return -1;
    }
    
    auto reqLen = static_cast<jsize>(request_len);
    jbyteArray reqData = jenv->NewByteArray(reqLen);
    if (reqData == nullptr) {
        detachJVM(jenv, envStat);
        return -1;
    }
    
    jenv->SetByteArrayRegion(reqData, 0, reqLen, (jbyte *) request);
    LOGD("BtExchange cmd: 0x%02x with %u bytes", request[0], reqLen);
    auto dataRecv = (jbyteArray) jenv->CallStaticObjectMethod(class_BluetoothService, exchangeMethod, reqData);
    jenv->DeleteLocalRef(reqData);
    if (dataRecv == nullptr) {
        detachJVM(jenv, envStat);
        LOGD("BtExchange: error reading");
        return -1;
    }
    jsize respLen = jenv->GetArrayLength(dataRecv);
    LOGD("BtExchange response is %u bytes", respLen);
    if (respLen <= max_resp_len) {
        jenv->GetByteArrayRegion(dataRecv, 0, respLen, (jbyte *) response);
        jenv->DeleteLocalRef(dataRecv);
        detachJVM(jenv, envStat);
        return static_cast<int>(respLen);
    } else {
        jenv->DeleteLocalRef(dataRecv);
        detachJVM(jenv, envStat);
        LOGE("BtExchange response buffer too small: %u < %u", respLen, max_resp_len);
        return -2;
    }
}

// MODIFIED: ConfirmTransfers function (Sidekick disabled)
bool ConfirmTransfers(const char *transfers) {
    // Sidekick functionality disabled - auto-confirm transfers
    LOGD("ConfirmTransfers: auto-confirming (Sidekick disabled)");
    return true;
}

} // extern "C"

// Helper functions implementations
std::vector<std::string> java2cpp(JNIEnv *env, jobject arrayList) {
    if (arrayList == nullptr) {
        return {};
    }
    
    jmethodID java_util_ArrayList_size = env->GetMethodID(class_ArrayList, "size", "()I");
    jmethodID java_util_ArrayList_get = env->GetMethodID(class_ArrayList, "get", "(I)Ljava/lang/Object;");
    
    if (java_util_ArrayList_size == nullptr || java_util_ArrayList_get == nullptr) {
        return {};
    }

    jint len = env->CallIntMethod(arrayList, java_util_ArrayList_size);
    std::vector<std::string> result;
    result.reserve(len);
    for (jint i = 0; i < len; i++) {
        jstring element = static_cast<jstring>(env->CallObjectMethod(arrayList, java_util_ArrayList_get, i));
        if (element != nullptr) {
            JNIStringGuard guard(env, element);
            if (guard.isValid()) {
                result.emplace_back(guard.get());
            }
        }
        if (element != nullptr) {
            env->DeleteLocalRef(element);
        }
    }
    return result;
}

jobject cpp2java(JNIEnv *env, const std::vector<std::string> &vector) {
    jmethodID java_util_ArrayList_ = env->GetMethodID(class_ArrayList, "<init>", "(I)V");
    jmethodID java_util_ArrayList_add = env->GetMethodID(class_ArrayList, "add", "(Ljava/lang/Object;)Z");
    
    if (java_util_ArrayList_ == nullptr || java_util_ArrayList_add == nullptr) {
        return nullptr;
    }

    jobject result = env->NewObject(class_ArrayList, java_util_ArrayList_, static_cast<jint> (vector.size()));
    for (const std::string &s: vector) {
        jstring element = env->NewStringUTF(s.c_str());
        env->CallBooleanMethod(result, java_util_ArrayList_add, element);
        env->DeleteLocalRef(element);
    }
    return result;
}

jobject newWalletStatusInstance(JNIEnv *env, int status, const std::string &errorString) {
    jmethodID init = env->GetMethodID(class_WalletStatus, "<init>", "(ILjava/lang/String;)V");
    if (init == nullptr) {
        return nullptr;
    }
    
    jstring _errorString = env->NewStringUTF(errorString.c_str());
    jobject instance = env->NewObject(class_WalletStatus, init, status, _errorString);
    env->DeleteLocalRef(_errorString);
    return instance;
}

jobject newTransferInstance(JNIEnv *env, uint64_t amount, const std::string &address) {
    jmethodID c = env->GetMethodID(class_Transfer, "<init>", "(JLjava/lang/String;)V");
    if (c == nullptr) {
        return nullptr;
    }
    
    jstring _address = env->NewStringUTF(address.c_str());
    jobject transfer = env->NewObject(class_Transfer, c, static_cast<jlong> (amount), _address);
    env->DeleteLocalRef(_address);
    return transfer;
}

jobject newTransferList(JNIEnv *env, Monero::TransactionInfo *info) {
    if (info == nullptr) {
        return nullptr;
    }
    
    const std::vector<Monero::TransactionInfo::Transfer> &transfers = info->transfers();
    if (transfers.empty()) {
        return nullptr;
    }
    
    jmethodID java_util_ArrayList_ = env->GetMethodID(class_ArrayList, "<init>", "(I)V");
    jmethodID java_util_ArrayList_add = env->GetMethodID(class_ArrayList, "add", "(Ljava/lang/Object;)Z");
    
    if (java_util_ArrayList_ == nullptr || java_util_ArrayList_add == nullptr) {
        return nullptr;
    }
    
    jobject result = env->NewObject(class_ArrayList, java_util_ArrayList_, static_cast<jint> (transfers.size()));
    for (const Monero::TransactionInfo::Transfer &s: transfers) {
        jobject element = newTransferInstance(env, s.amount, s.address);
        if (element != nullptr) {
            env->CallBooleanMethod(result, java_util_ArrayList_add, element);
            env->DeleteLocalRef(element);
        }
    }
    return result;
}

jobject newTransactionInfo(JNIEnv *env, Monero::TransactionInfo *info) {
    if (info == nullptr) {
        return nullptr;
    }
    
    jmethodID c = env->GetMethodID(class_TransactionInfo, "<init>",
                                   "(IZZJJJLjava/lang/String;JLjava/lang/String;IIJJLjava/lang/String;Ljava/util/List;)V");
    if (c == nullptr) {
        return nullptr;
    }
    
    jobject transfers = newTransferList(env, info);
    jstring _hash = env->NewStringUTF(info->hash().c_str());
    jstring _paymentId = env->NewStringUTF(info->paymentId().c_str());
    jstring _label = env->NewStringUTF(info->label().c_str());
    
    uint32_t subaddrIndex = 0;
    if (info->direction() == Monero::TransactionInfo::Direction_In && !info->subaddrIndex().empty()) {
        subaddrIndex = *(info->subaddrIndex().begin());
    }
    
    jobject result = env->NewObject(class_TransactionInfo, c,
                                    info->direction(),
                                    info->isPending(),
                                    info->isFailed(),
                                    static_cast<jlong> (info->amount()),
                                    static_cast<jlong> (info->fee()),
                                    static_cast<jlong> (info->blockHeight()),
                                    _hash,
                                    static_cast<jlong> (info->timestamp()),
                                    _paymentId,
                                    static_cast<jint> (info->subaddrAccount()),
                                    static_cast<jint> (subaddrIndex),
                                    static_cast<jlong> (info->confirmations()),
                                    static_cast<jlong> (info->unlockTime()),
                                    _label,
                                    transfers);
    
    // Clean up local references
    if (transfers != nullptr) {
        env->DeleteLocalRef(transfers);
    }
    env->DeleteLocalRef(_hash);
    env->DeleteLocalRef(_paymentId);
    env->DeleteLocalRef(_label);
    return result;
}

jobject transactionInfoArrayList(JNIEnv *env, const std::vector<Monero::TransactionInfo *> &vector, uint32_t accountIndex) {
    jmethodID java_util_ArrayList_ = env->GetMethodID(class_ArrayList, "<init>", "(I)V");
    jmethodID java_util_ArrayList_add = env->GetMethodID(class_ArrayList, "add", "(Ljava/lang/Object;)Z");
    
    if (java_util_ArrayList_ == nullptr || java_util_ArrayList_add == nullptr) {
        return nullptr;
    }

    jobject arrayList = env->NewObject(class_ArrayList, java_util_ArrayList_, static_cast<jint> (vector.size()));
    for (Monero::TransactionInfo *s: vector) {
        if (s == nullptr || s->subaddrAccount() != accountIndex) continue;
        jobject info = newTransactionInfo(env, s);
        if (info != nullptr) {
            env->CallBooleanMethod(arrayList, java_util_ArrayList_add, info);
            env->DeleteLocalRef(info);
        }
    }
    return arrayList;
}

void logWalletCreation(Monero::Wallet* wallet, const char* method) {
    std::lock_guard<std::mutex> lock(g_walletTrackerMutex);
    g_walletTracker[wallet] = method;
    LOGD("WALLET_CREATE: %p created by %s (total wallets: %zu)", 
         wallet, method, g_walletTracker.size());
}

void logWalletDeletion(Monero::Wallet* wallet) {
    std::lock_guard<std::mutex> lock(g_walletTrackerMutex);
    auto it = g_walletTracker.find(wallet);
    if (it != g_walletTracker.end()) {
        LOGD("WALLET_DELETE: %p deleting (created by: %s)", 
             wallet, it->second.c_str());
        g_walletTracker.erase(it);
        LOGD("WALLET_DELETE: remaining wallets: %zu", g_walletTracker.size());
    } else {
        LOGW("WALLET_DELETE: %p not found in tracker", wallet);
    }
}

void logAllWallets() {
    std::lock_guard<std::mutex> lock(g_walletTrackerMutex);
    LOGD("WALLET_TRACKER: Current wallets (%zu):", g_walletTracker.size());
    for (const auto& pair : g_walletTracker) {
        LOGD("  - %p (created by: %s)", pair.first, pair.second.c_str());
    }
}
