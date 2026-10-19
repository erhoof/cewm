#include "haptic.h"
#ifdef CEWM_HAVE_DBUS
#include <dbus/dbus.h>

namespace cewm {
void HapticTick() {
    // ngfd drops an event when its caller disconnects, so the connection stays open for the process lifetime.
    static DBusConnection* c = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        c = dbus_bus_get_private(DBUS_BUS_SYSTEM, nullptr);
        if (c) dbus_connection_set_exit_on_disconnect(c, FALSE);
    }
    if (!c) return;
    DBusMessage* m = dbus_message_new_method_call("com.nokia.NonGraphicFeedback1.Backend", "/com/nokia/NonGraphicFeedback1",
                                                  "com.nokia.NonGraphicFeedback1", "Play");
    if (!m) return;
    dbus_message_set_no_reply(m, TRUE);
    const char* event = "feedback_press";  // "vibra" is the 100+500+100 ms notice pattern: two pulses
    dbus_message_append_args(m, DBUS_TYPE_STRING, &event, DBUS_TYPE_INVALID);
    DBusMessageIter it, arr;
    dbus_message_iter_init_append(m, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
    dbus_message_iter_close_container(&it, &arr);
    dbus_connection_send(c, m, nullptr);
    dbus_connection_flush(c);
    dbus_message_unref(m);
}
}  // namespace cewm
#else
namespace cewm { void HapticTick() {} }
#endif
