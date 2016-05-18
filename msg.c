#include "globalconf.h"
#include "msg.h"

#include <assert.h>
#include <webkit2/webkit2.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "clib/web_module.h"
#include "common/luaserialize.h"

void webview_scroll_recv(void *d, const msg_scroll_t *msg);

void
msg_recv_lua_require_module(const msg_lua_require_module_t *UNUSED(msg), guint UNUSED(length))
{
    fatal("UI process should never receive message of this type");
}

void
msg_recv_lua_msg(const msg_lua_msg_t *msg, guint length)
{
    const guint module = msg->module;
    const char *arg = msg->arg;

    web_module_recv(globalconf.L, module, arg, length-sizeof(module));
}

void
msg_recv_scroll(msg_scroll_t *msg, guint UNUSED(length))
{
    g_ptr_array_foreach(globalconf.webviews, (GFunc)webview_scroll_recv, msg);
}

void
msg_recv_rc_loaded(const msg_lua_require_module_t *UNUSED(msg), guint UNUSED(length))
{
    fatal("UI process should never receive message of this type");
}

void
msg_recv_lua_js_call(const guint8 *msg, guint length)
{
    lua_State *L = globalconf.L;
    gint top = lua_gettop(L);

    /* Deserialize the Lua we got */
    int argc = lua_deserialize_range(L, msg, length) - 1;
    gpointer ref = lua_topointer(L, top + 1);
    lua_remove(L, top+1);

    /* push Lua callback function into position */
    luaH_object_push(L, ref);
    lua_insert(L, -argc-1);
    /* Call the function; push result/error and ok/error boolean */
    lua_pushboolean(L, lua_pcall(L, argc, 1, 0));

    /* Serialize the result, and send it back */
    msg_send_lua(MSG_TYPE_lua_js_call, L, -2, -1);
    lua_settop(L, top);
}

void
msg_recv_lua_js_gc(const guint8 *msg, guint length)
{
    lua_State *L = globalconf.L;
    /* Unref the function reference we got */
    gint n = lua_deserialize_range(L, msg, length);
    g_assert_cmpint(n, ==, 1);
    luaH_object_unref(L, lua_topointer(L, -1));
    lua_pop(L, 1);
}

void
msg_recv_lua_js_register(gpointer UNUSED(msg), guint UNUSED(length))
{
    fatal("UI process should never receive message of this type");
}

static gpointer
web_extension_connect(gpointer user_data)
{
    const gchar *socket_path = user_data;

    int sock, web_socket;
    struct sockaddr_un local, remote;
    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, socket_path);
    int len = strlen(local.sun_path) + sizeof(local.sun_family);

    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
        fatal("Can't open new socket");

    /* Remove any pre-existing socket, before opening */
    unlink(local.sun_path);

    if (bind(sock, (struct sockaddr *)&local, len) == -1)
        fatal("Can't bind socket to %s", socket_path);

    if (listen(sock, 5) == -1)
        fatal("Can't listen on %s", socket_path);

    debug("Waiting for a connection...");

    socklen_t size = sizeof(remote);
    if ((web_socket = accept(sock, (struct sockaddr *)&remote, &size)) == -1)
        fatal("Can't accept on %s", socket_path);

    close(sock);

    debug("Creating channel...");

    globalconf.web_channel = msg_setup(web_socket);

    /* Send all queued messages */
    g_io_channel_write_chars(globalconf.web_channel, (gchar*)globalconf.web_channel_queue->data, globalconf.web_channel_queue->len, NULL, NULL);
    g_byte_array_unref(globalconf.web_channel_queue);
    globalconf.web_channel_queue = NULL;

    return NULL;
}

static void
initialize_web_extensions_cb(WebKitWebContext *context, gpointer UNUSED(user_data))
{
    const gchar *socket_path = g_build_filename(globalconf.cache_dir, "socket", NULL);
    /* const gchar *extension_dir = "/home/aidan/Programming/luakit/luakit/"; */
    const gchar *extension_dir = LUAKIT_INSTALL_PATH;

    /* Set up connection to web extension process */
    g_thread_new("accept_thread", web_extension_connect, (void*)socket_path);

    /* There's a potential race condition here; the accept thread might not run
     * until after the web extension process has already started (and failed to
     * connect). TODO: add a busy wait */

    GVariant *payload = g_variant_new_string(socket_path);
    webkit_web_context_set_web_extensions_initialization_user_data(context, payload);
    webkit_web_context_set_web_extensions_directory(context, extension_dir);
}

void
msg_init(void)
{
    globalconf.web_channel_queue = g_byte_array_new();
    g_signal_connect(webkit_web_context_get_default(), "initialize-web-extensions",
            G_CALLBACK (initialize_web_extensions_cb), NULL);
}

void
msg_send(const msg_header_t *header, const void *data)
{
    if (globalconf.web_channel) {
        g_io_channel_write_chars(globalconf.web_channel, (gchar*)header, sizeof(*header), NULL, NULL);
        g_io_channel_write_chars(globalconf.web_channel, (gchar*)data, header->length, NULL, NULL);
    } else {
        g_byte_array_append(globalconf.web_channel_queue, (guint8*)header, sizeof(*header));
        g_byte_array_append(globalconf.web_channel_queue, (guint8*)data, header->length);
    }
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
