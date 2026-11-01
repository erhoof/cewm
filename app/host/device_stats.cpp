#include "device_stats.h"
#include <cstdio>
#include <cstdlib>

#include <chrono>
#include <cstring>
#include <thread>

#include "host_state.h"

#ifdef __ANDROID__
#include <SDL.h>
#include <jni.h>
#include <sstream>
#include "../android/app_class.h"
#endif

#ifdef CEWM_HAVE_DBUS
#include <dbus/dbus.h>
namespace {
// Calls a method with no arguments and returns the reply (caller unrefs), or null.
DBusMessage* Call(DBusConnection* c, const char* dest, const char* path, const char* iface, const char* method) {
    DBusMessage* m = dbus_message_new_method_call(dest, path, iface, method);
    if (!m) return nullptr;
    DBusMessage* r = dbus_connection_send_with_reply_and_block(c, m, 1500, nullptr);
    dbus_message_unref(m);
    return r;
}

// Dict entry "key" -> variant basic value; returns true when found. Walks a{sv} at the iterator.
bool DictGet(DBusMessageIter dict, const char* key, DBusBasicValue* out, int* type) {
    for (; dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY; dbus_message_iter_next(&dict)) {
        DBusMessageIter e, v;
        dbus_message_iter_recurse(&dict, &e);
        const char* k;
        dbus_message_iter_get_basic(&e, &k);
        if (strcmp(k, key)) continue;
        dbus_message_iter_next(&e);
        dbus_message_iter_recurse(&e, &v);
        *type = dbus_message_iter_get_arg_type(&v);
        dbus_message_iter_get_basic(&v, out);
        return true;
    }
    return false;
}

void Battery(DBusConnection* c) {
    auto geti = [&](const char* m, int32_t* i, const char** s) {
        DBusMessage* r = Call(c, "com.nokia.mce", "/com/nokia/mce/request", "com.nokia.mce.request", m);
        if (!r) return false;
        DBusMessageIter it;
        bool ok = dbus_message_iter_init(r, &it);
        if (ok && i) dbus_message_iter_get_basic(&it, i);
        if (ok && s) dbus_message_iter_get_basic(&it, s);
        dbus_message_unref(r);
        return ok;
    };
    int32_t lvl = 0;
    if (geti("get_battery_level", &lvl, nullptr)) cewm::Host().battery_percent = lvl;
    const char* ch = "";
    const bool okc = geti("get_charger_state", nullptr, &ch);
    if (okc) cewm::Host().on_battery = strcmp(ch, "on") != 0;
    if (getenv("CEWM_STATSLOG")) fprintf(stderr, "[STATS] battery %d%% charger=%s%s\n", (int)lvl, okc ? ch : "?", okc ? "" : " (MCE call failed)");
}

// Operator name from the DeviceInfo service (the DeviceInfo permission; org.ofono would need the Phone permission, which the store
// validator rejects). It has no signal strength, so a registered SIM shows full bars for now; network type and PLMN stay at their defaults.
void Operator(DBusConnection* c) {
    DBusMessage* r = Call(c, "ru.omp.deviceinfo", "/ru/omp/deviceinfo/SIM", "ru.omp.deviceinfo.SIM", "getSimCardsInfo");
    if (getenv("CEWM_STATSLOG")) fprintf(stderr, "[STATS] deviceinfo %s\n", r ? "ok" : "call failed");
    if (!r) { cewm::Host().cell_signal = -1; return; }  // set only on failure: a reset before the call made the guest briefly see "no service"
    int strength = -1;
    DBusMessageIter it, sims, d;
    if (dbus_message_iter_init(r, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&it, &sims);
        for (; dbus_message_iter_get_arg_type(&sims) == DBUS_TYPE_ARRAY; dbus_message_iter_next(&sims)) {  // one a{sv} per SIM slot
            dbus_message_iter_recurse(&sims, &d);
            DBusBasicValue v; int t;
            if (!DictGet(d, "enabled", &v, &t) || t != DBUS_TYPE_BOOLEAN || !v.bool_val) continue;
            if (DictGet(d, "operatorName", &v, &t) && t == DBUS_TYPE_STRING && v.str[0]) {
                std::lock_guard<std::mutex> lk(cewm::Host().info_mu);
                cewm::Host().operator_name = v.str;
                strength = 100;
                break;
            }
        }
    }
    cewm::Host().cell_signal = strength;
    dbus_message_unref(r);
}

// GetServices -> a(oa{sv}); first service of type wifi in state ready/online.
void Wifi(DBusConnection* c) {
    cewm::Host().wifi_signal = -1;
    DBusMessage* r = Call(c, "net.connman", "/", "net.connman.Manager", "GetServices");
    if (!r) return;
    DBusMessageIter it, arr;
    if (dbus_message_iter_init(r, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&it, &arr);
        for (; dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT; dbus_message_iter_next(&arr)) {
            DBusMessageIter st, d;
            dbus_message_iter_recurse(&arr, &st);
            dbus_message_iter_next(&st);
            dbus_message_iter_recurse(&st, &d);
            DBusBasicValue ty, state, nm, sg; int t;
            if (!DictGet(d, "Type", &ty, &t) || strcmp(ty.str, "wifi")) continue;
            if (!DictGet(d, "State", &state, &t) || (strcmp(state.str, "online") && strcmp(state.str, "ready"))) continue;
            if (DictGet(d, "Name", &nm, &t)) {
                std::lock_guard<std::mutex> lk(cewm::Host().info_mu);
                cewm::Host().wifi_name = nm.str;
            }
            if (DictGet(d, "Strength", &sg, &t)) cewm::Host().wifi_signal = sg.byt;
            break;
        }
    }
    dbus_message_unref(r);
}
}  // namespace
#endif

namespace cewm {
void StartDeviceStats() {
#ifdef __ANDROID__
    // ru.erhoof.cewm.Stats.poll() answers "key=value" lines: batt, charging, operator, plmn, act, cell, wifi (see Stats.java).
    std::thread([] {
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        jclass cls = AppClass(env, "ru.erhoof.cewm.Stats");
        if (!cls) return;
        jmethodID poll = env->GetStaticMethodID(cls, "poll", "()Ljava/lang/String;");
        for (;;) {
            jstring js = (jstring)env->CallStaticObjectMethod(cls, poll);
            if (env->ExceptionCheck()) env->ExceptionClear();
            else if (js) {
                const char* t = env->GetStringUTFChars(js, nullptr);
                static std::string last;
                if (last != t) { last = t; SDL_Log("stats: %s", t); }
                std::istringstream in(t);
                env->ReleaseStringUTFChars(js, t);
                for (std::string l; std::getline(in, l);) {
                    const size_t eq = l.find('=');
                    if (eq == std::string::npos) continue;
                    const std::string k = l.substr(0, eq), v = l.substr(eq + 1);
                    HostState& h = Host();
                    if (k == "batt") h.battery_percent = atoi(v.c_str());
                    else if (k == "charging") h.on_battery = v != "1";
                    else if (k == "cell") h.cell_signal = atoi(v.c_str());
                    else if (k == "wifi") h.wifi_signal = atoi(v.c_str());
                    else if (k == "act") h.cell_act = atoi(v.c_str());
                    else if (k == "operator") { std::lock_guard<std::mutex> lk(h.info_mu); h.operator_name = v; }
                    else if (k == "plmn") { std::lock_guard<std::mutex> lk(h.info_mu); h.operator_plmn = v; }
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }).detach();
#endif
#ifdef CEWM_HAVE_DBUS
    std::thread([] {
        DBusConnection* c = dbus_bus_get_private(DBUS_BUS_SYSTEM, nullptr);
        if (!c) return;
        dbus_connection_set_exit_on_disconnect(c, FALSE);
        for (;;) {
            Battery(c); Operator(c); Wifi(c);
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }).detach();
#endif
}
}  // namespace cewm
