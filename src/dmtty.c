#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmtty.h"
#include "dmdrvi.h"
#include "dmhaman.h"
#include "dmini.h"
#include "dmlist.h"
#include "dmosi.h"
#include <errno.h>
#include <string.h>

/* Magic set to "DTTY" */
#define DMTTY_CONTEXT_MAGIC     0x44545459

/**
 * @brief Size of the internal line-assembly buffer used in canonical mode
 *
 * A line longer than this is handed back to the caller once full, exactly
 * like hitting a line terminator - it is a safety cap, not a hard protocol
 * limit.
 */
#define DMTTY_LINE_BUFFER_SIZE  128

/**
 * @brief A single exposed device node: a device number plus the backing
 *        file it forwards to/from and the line discipline applied to it
 */
typedef struct
{
    dmdrvi_dev_num_t dev_num;                          /**< Device number this slot is exposed under */
    char            *backing_path;                      /**< Path forwarded to/from (NULL = unbound), owned by this slot */
    uint32_t         flags;                             /**< Current IO flags (dmtty_flags_t bitmask) */
    int              open_count;                        /**< Number of handles currently open on this slot */
} dmtty_slot_t;

/**
 * @brief DMDRVI context structure - one per dmtty deployment (the "/dev/tty" context)
 *
 * A single context exposes many device nodes (the main /dev/tty plus any
 * attached via dmtty_attach() or the dmhaman hot-plug event), all sharing
 * this context as documented by dmdrvi_device_available().
 */
struct dmdrvi_context
{
    uint32_t           magic;          /**< Magic number for validation */
    dmlist_context_t  *slots;          /**< List of dmtty_slot_t* exposed by this context */
    dmosi_mutex_t      lock;           /**< Guards `slots` and each slot's mutable fields */
    uint32_t           next_auto_index; /**< Monotonic counter used to name auto-numbered nodes ("ttyN") */
};

/**
 * @brief Per-open handle
 *
 * Holds the actual open backing file plus the per-open line assembly state,
 * so that two opens of the same node keep independent read progress.
 */
typedef struct
{
    dmtty_slot_t *slot;                          /**< Slot this handle was opened from */
    void         *backing_file;                  /**< Backing file, opened via Dmod_FileOpen() */
    char          line_buf[DMTTY_LINE_BUFFER_SIZE]; /**< Canonical-mode line assembly buffer */
    size_t        line_len;                       /**< Bytes currently assembled into line_buf */
    size_t        line_pos;                       /**< Read offset into a completed line */
    bool          have_line;                      /**< True once line_buf holds a complete line */
} dmtty_handle_t;

/**
 * @brief The single dmtty context created from configuration (the "/dev/tty" one)
 *
 * dmtty_attach()/dmtty_detach() (the direct API) operate on this context so
 * callers do not need to carry a dmdrvi_context_t around themselves.
 */
static dmdrvi_context_t g_default_context = NULL;

static bool is_valid_context(dmdrvi_context_t context);
static bool string_to_bool(const char *s, bool default_value);
static bool slot_name_matches(const dmtty_slot_t *slot, const char *name);
static dmtty_slot_t *find_slot_by_dev_num(dmdrvi_context_t context, const dmdrvi_dev_num_t *dev_num);
static dmtty_slot_t *find_slot_by_name_locked(dmdrvi_context_t context, const char *name);
static dmtty_slot_t *find_slot_by_backing_path_locked(dmdrvi_context_t context, const char *backing_path);
static int compare_slot_ptr(const void *data1, const void *data2);
static int attach_internal(dmdrvi_context_t context, const char *backing_path, const char *name,
                            uint32_t flags, dmdrvi_dev_num_t *dev_num_out);
static int detach_internal(dmdrvi_context_t context, const char *name);
static int on_device_available(void *parameters, void *user_ctx);
static int on_device_unavailable(void *parameters, void *user_ctx);

/* ---- Helpers ---- */

static bool is_valid_context(dmdrvi_context_t context)
{
    return context != NULL && context->magic == DMTTY_CONTEXT_MAGIC;
}

static bool string_to_bool(const char *s, bool default_value)
{
    if (s == NULL)
    {
        return default_value;
    }
    if (strcmp(s, "off") == 0 || strcmp(s, "0") == 0 || strcmp(s, "false") == 0)
    {
        return false;
    }
    if (strcmp(s, "on") == 0 || strcmp(s, "1") == 0 || strcmp(s, "true") == 0)
    {
        return true;
    }
    return default_value;
}

static bool slot_name_matches(const dmtty_slot_t *slot, const char *name)
{
    return (slot->dev_num.flags & DMDRVI_NUM_ALT_NAME) != 0 &&
           strncmp(slot->dev_num.alt_name, name, sizeof(slot->dev_num.alt_name)) == 0;
}

/* Caller must hold context->lock */
static dmtty_slot_t *find_slot_by_dev_num(dmdrvi_context_t context, const dmdrvi_dev_num_t *dev_num)
{
    size_t count = dmlist_size(context->slots);
    for (size_t i = 0; i < count; i++)
    {
        dmtty_slot_t *slot = (dmtty_slot_t *)dmlist_get(context->slots, i);
        if (slot->dev_num.flags == dev_num->flags &&
            slot->dev_num.major == dev_num->major &&
            slot->dev_num.minor == dev_num->minor &&
            strncmp(slot->dev_num.alt_name, dev_num->alt_name, sizeof(slot->dev_num.alt_name)) == 0)
        {
            return slot;
        }
    }
    return NULL;
}

/* Caller must hold context->lock */
static dmtty_slot_t *find_slot_by_name_locked(dmdrvi_context_t context, const char *name)
{
    size_t count = dmlist_size(context->slots);
    for (size_t i = 0; i < count; i++)
    {
        dmtty_slot_t *slot = (dmtty_slot_t *)dmlist_get(context->slots, i);
        if (slot_name_matches(slot, name))
        {
            return slot;
        }
    }
    return NULL;
}

/* Caller must hold context->lock */
static dmtty_slot_t *find_slot_by_backing_path_locked(dmdrvi_context_t context, const char *backing_path)
{
    size_t count = dmlist_size(context->slots);
    for (size_t i = 0; i < count; i++)
    {
        dmtty_slot_t *slot = (dmtty_slot_t *)dmlist_get(context->slots, i);
        if (slot->backing_path != NULL && strcmp(slot->backing_path, backing_path) == 0)
        {
            return slot;
        }
    }
    return NULL;
}

static int compare_slot_ptr(const void *data1, const void *data2)
{
    return (data1 == data2) ? 0 : 1;
}

/**
 * @brief Shared implementation behind dmtty_attach() and the
 *        DMTTY_HANDLER_NAME_DEVICE_AVAILABLE dmhaman event
 */
static int attach_internal(dmdrvi_context_t context, const char *backing_path, const char *name,
                            uint32_t flags, dmdrvi_dev_num_t *dev_num_out)
{
    if (!is_valid_context(context) || backing_path == NULL || backing_path[0] == '\0')
    {
        return -EINVAL;
    }

    if (name != NULL && strlen(name) > DMDRVI_ALT_NAME_MAX_LEN)
    {
        return -ENAMETOOLONG;
    }

    dmosi_mutex_lock(context->lock);

    char name_buf[DMDRVI_ALT_NAME_MAX_LEN + 1];
    if (name != NULL)
    {
        if (find_slot_by_name_locked(context, name) != NULL)
        {
            dmosi_mutex_unlock(context->lock);
            return -EEXIST;
        }
        Dmod_SnPrintf(name_buf, sizeof(name_buf), "%s", name);
    }
    else
    {
        do
        {
            Dmod_SnPrintf(name_buf, sizeof(name_buf), "tty%u", context->next_auto_index++);
        } while (find_slot_by_name_locked(context, name_buf) != NULL);
    }

    dmtty_slot_t *slot = Dmod_Malloc(sizeof(dmtty_slot_t));
    if (slot == NULL)
    {
        dmosi_mutex_unlock(context->lock);
        return -ENOMEM;
    }
    memset(slot, 0, sizeof(*slot));
    slot->dev_num.flags = DMDRVI_NUM_ALT_NAME;
    Dmod_SnPrintf(slot->dev_num.alt_name, sizeof(slot->dev_num.alt_name), "%s", name_buf);
    slot->backing_path = Dmod_StrDup(backing_path);
    slot->flags = (flags != 0) ? flags : DMTTY_FLAGS_DEFAULT;

    if (slot->backing_path == NULL || !dmlist_push_back(context->slots, slot))
    {
        Dmod_Free(slot->backing_path);
        Dmod_Free(slot);
        dmosi_mutex_unlock(context->lock);
        return -ENOMEM;
    }

    dmdrvi_dev_num_t new_dev_num = slot->dev_num;
    dmosi_mutex_unlock(context->lock);

    /* Notify dmdevfs outside the lock: it may call back into this driver
     * (e.g. dmtty_dmdrvi_open) from a hot-plug worker thread. */
    dmdrvi_device_available(context, &new_dev_num);

    if (dev_num_out != NULL)
    {
        *dev_num_out = new_dev_num;
    }

    DMOD_LOG_INFO("dmtty: attached '%s' -> %s\n", name_buf, backing_path);
    return 0;
}

/**
 * @brief Shared implementation behind dmtty_detach()
 */
static int detach_internal(dmdrvi_context_t context, const char *name)
{
    if (!is_valid_context(context) || name == NULL || name[0] == '\0')
    {
        return -EINVAL;
    }

    if (strcmp(name, "tty") == 0)
    {
        /* The main node was created from configuration by dmdevfs itself and
         * is not a dynamic (hot-plugged) node - it may only go away when the
         * whole context is freed via dmtty_dmdrvi_free(). */
        return -EPERM;
    }

    dmosi_mutex_lock(context->lock);
    dmtty_slot_t *slot = find_slot_by_name_locked(context, name);
    if (slot == NULL)
    {
        dmosi_mutex_unlock(context->lock);
        return -ENOENT;
    }
    if (slot->open_count > 0)
    {
        dmosi_mutex_unlock(context->lock);
        return -EBUSY;
    }

    dmdrvi_dev_num_t dev_num = slot->dev_num;
    dmlist_remove(context->slots, slot, compare_slot_ptr);
    dmosi_mutex_unlock(context->lock);

    dmdrvi_device_unavailable(context, &dev_num);
    Dmod_Free(slot->backing_path);
    Dmod_Free(slot);

    DMOD_LOG_INFO("dmtty: detached '%s'\n", name);
    return 0;
}

/**
 * @brief dmhaman callback registered under DMTTY_HANDLER_NAME_DEVICE_AVAILABLE
 *
 * Lets a driver (e.g. dmuart, once it knows its own device path) announce
 * itself as TTY-compatible without a compile-time dependency on dmtty.
 */
static int on_device_available(void *parameters, void *user_ctx)
{
    dmdrvi_context_t context = (dmdrvi_context_t)user_ctx;
    const dmtty_device_available_params_t *params = (const dmtty_device_available_params_t *)parameters;

    if (params == NULL || params->path == NULL)
    {
        DMOD_LOG_ERROR("dmtty: invalid device_available event parameters\n");
        return -EINVAL;
    }

    uint32_t flags = (params->flags != 0) ? params->flags : DMTTY_FLAGS_DEFAULT;
    int ret = attach_internal(context, params->path, params->name, flags, NULL);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("dmtty: failed to attach '%s' from device_available event: %d\n", params->path, ret);
    }
    return ret;
}

/**
 * @brief dmhaman callback registered under DMTTY_HANDLER_NAME_DEVICE_UNAVAILABLE
 *
 * Counterpart of on_device_available(): detaches the node(s) previously
 * attached for a backing device that is now going away.
 */
static int on_device_unavailable(void *parameters, void *user_ctx)
{
    dmdrvi_context_t context = (dmdrvi_context_t)user_ctx;
    const dmtty_device_unavailable_params_t *params = (const dmtty_device_unavailable_params_t *)parameters;

    if (params == NULL || params->path == NULL)
    {
        DMOD_LOG_ERROR("dmtty: invalid device_unavailable event parameters\n");
        return -EINVAL;
    }

    dmosi_mutex_lock(context->lock);
    dmtty_slot_t *slot = find_slot_by_backing_path_locked(context, params->path);
    char name_buf[DMDRVI_ALT_NAME_MAX_LEN + 1];
    if (slot != NULL)
    {
        Dmod_SnPrintf(name_buf, sizeof(name_buf), "%s", slot->dev_num.alt_name);
    }
    dmosi_mutex_unlock(context->lock);

    if (slot == NULL)
    {
        DMOD_LOG_WARN("dmtty: no node backed by '%s' to detach from device_unavailable event\n", params->path);
        return -ENOENT;
    }

    int ret = detach_internal(context, name_buf);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("dmtty: failed to detach '%s' from device_unavailable event: %d\n", name_buf, ret);
    }
    return ret;
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    DMOD_LOG_INFO("DMTTY module initialized\n");
    return 0;
}

int dmod_deinit(void)
{
    DMOD_LOG_INFO("DMTTY module deinitialized\n");
    return 0;
}

/* ---- Direct API (dmtty.h) ---- */

int dmtty_attach(const char *backing_path, const char *name, uint32_t flags)
{
    if (g_default_context == NULL)
    {
        DMOD_LOG_ERROR("dmtty_attach: dmtty has not been configured yet (no /dev/tty context)\n");
        return -ENODEV;
    }
    return attach_internal(g_default_context, backing_path, name, flags, NULL);
}

int dmtty_detach(const char *name)
{
    if (g_default_context == NULL)
    {
        return -ENODEV;
    }
    return detach_internal(g_default_context, name);
}

/* ---- DMDRVI interface ---- */

dmod_dmdrvi_dif_api_declaration(1.0, dmtty, dmdrvi_context_t, _create, ( dmini_context_t config, dmdrvi_dev_num_t* dev_num ))
{
    if (dev_num == NULL)
    {
        DMOD_LOG_ERROR("Invalid parameters to dmtty_dmdrvi_create\n");
        return NULL;
    }

    if (g_default_context != NULL)
    {
        DMOD_LOG_ERROR("dmtty is already configured - only one /dev/tty context is supported\n");
        return NULL;
    }

    dmdrvi_context_t context = Dmod_Malloc(sizeof(struct dmdrvi_context));
    if (context == NULL)
    {
        return NULL;
    }
    memset(context, 0, sizeof(*context));
    context->magic = DMTTY_CONTEXT_MAGIC;
    context->slots = dmlist_create(DMOD_MODULE_NAME);
    context->lock  = dmosi_mutex_create(false);
    if (context->slots == NULL || context->lock == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate dmtty context resources\n");
        dmlist_destroy(context->slots);
        dmosi_mutex_destroy(context->lock);
        Dmod_Free(context);
        return NULL;
    }

    dmtty_slot_t *main_slot = Dmod_Malloc(sizeof(dmtty_slot_t));
    if (main_slot == NULL)
    {
        dmlist_destroy(context->slots);
        dmosi_mutex_destroy(context->lock);
        Dmod_Free(context);
        return NULL;
    }
    memset(main_slot, 0, sizeof(*main_slot));
    main_slot->dev_num.flags = DMDRVI_NUM_ALT_NAME;
    Dmod_SnPrintf(main_slot->dev_num.alt_name, sizeof(main_slot->dev_num.alt_name), "tty");

    /* [dmtty] section is optional: with no config at all dmtty still creates
     * an (unbound) /dev/tty with the usual echo+canonical defaults, ready to
     * be pointed at a backing file later via dmtty_attach()/ioctl. */
    const char *backing = (config != NULL) ? dmini_get_string(config, "dmtty", "backing", NULL) : NULL;
    if (backing != NULL)
    {
        main_slot->backing_path = Dmod_StrDup(backing);
        if (main_slot->backing_path == NULL)
        {
            DMOD_LOG_ERROR("dmtty: failed to allocate 'backing' path from configuration\n");
            Dmod_Free(main_slot);
            dmlist_destroy(context->slots);
            dmosi_mutex_destroy(context->lock);
            Dmod_Free(context);
            return NULL;
        }
    }

    bool echo_enabled = string_to_bool((config != NULL) ? dmini_get_string(config, "dmtty", "echo", "on") : "on", true);
    bool canonical_enabled = string_to_bool((config != NULL) ? dmini_get_string(config, "dmtty", "canonical", "on") : "on", true);
    main_slot->flags = (echo_enabled ? dmtty_flag_echo : 0) | (canonical_enabled ? dmtty_flag_canonical : 0);

    if (!dmlist_push_back(context->slots, main_slot))
    {
        DMOD_LOG_ERROR("dmtty: failed to register the main /dev/tty node\n");
        Dmod_Free(main_slot);
        dmlist_destroy(context->slots);
        dmosi_mutex_destroy(context->lock);
        Dmod_Free(context);
        return NULL;
    }

    if (dmhaman_register_handler(DMTTY_HANDLER_NAME_DEVICE_AVAILABLE, on_device_available, context) != 0)
    {
        DMOD_LOG_WARN("dmtty: failed to register '%s' dmhaman handler - hot-plug announcements will be ignored\n",
                      DMTTY_HANDLER_NAME_DEVICE_AVAILABLE);
    }

    if (dmhaman_register_handler(DMTTY_HANDLER_NAME_DEVICE_UNAVAILABLE, on_device_unavailable, context) != 0)
    {
        DMOD_LOG_WARN("dmtty: failed to register '%s' dmhaman handler - hot-unplug announcements will be ignored\n",
                      DMTTY_HANDLER_NAME_DEVICE_UNAVAILABLE);
    }

    *dev_num = main_slot->dev_num;
    g_default_context = context;

    DMOD_LOG_INFO("dmtty configured: alt_name='%s'%s%s (echo=%s, canonical=%s)\n",
        main_slot->dev_num.alt_name,
        main_slot->backing_path != NULL ? " backed by " : "",
        main_slot->backing_path != NULL ? main_slot->backing_path : "",
        echo_enabled ? "on" : "off",
        canonical_enabled ? "on" : "off");

    return context;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmtty, void, _free, ( dmdrvi_context_t context ))
{
    if (!is_valid_context(context))
    {
        return;
    }

    dmhaman_unregister_handler(DMTTY_HANDLER_NAME_DEVICE_AVAILABLE, on_device_available);
    dmhaman_unregister_handler(DMTTY_HANDLER_NAME_DEVICE_UNAVAILABLE, on_device_unavailable);

    size_t count = dmlist_size(context->slots);
    for (size_t i = 0; i < count; i++)
    {
        dmtty_slot_t *slot = dmlist_get(context->slots, i);
        Dmod_Free(slot->backing_path);
        Dmod_Free(slot);
    }
    dmlist_destroy(context->slots);
    dmosi_mutex_destroy(context->lock);

    if (g_default_context == context)
    {
        g_default_context = NULL;
    }

    context->magic = 0;
    Dmod_Free(context);
}

/*
 * NOTE on the `_open` signature: dmdrvi.h documents dmdrvi_open() as taking a
 * trailing `const dmdrvi_dev_num_t*` so dmdevfs can tell the driver which of
 * a context's devices is being opened. dmtty relies on that: unlike a driver
 * with one device per context (e.g. dmuart, dmfmc), a single dmtty context
 * exposes the main /dev/tty node plus every node created via dmtty_attach()
 * or a dmhaman hot-plug event, so dev_num is required to pick the right one.
 */
dmod_dmdrvi_dif_api_declaration(1.0, dmtty, void*, _open, ( dmdrvi_context_t context, int flags, const dmdrvi_dev_num_t* dev_num ))
{
    if (!is_valid_context(context) || dev_num == NULL)
    {
        DMOD_LOG_ERROR("Invalid parameters in dmtty_dmdrvi_open\n");
        return NULL;
    }

    dmosi_mutex_lock(context->lock);
    dmtty_slot_t *slot = find_slot_by_dev_num(context, dev_num);
    if (slot != NULL)
    {
        slot->open_count++;
    }
    dmosi_mutex_unlock(context->lock);

    if (slot == NULL)
    {
        DMOD_LOG_ERROR("dmtty: no device node matches the requested dev_num\n");
        return NULL;
    }

    if (slot->backing_path == NULL)
    {
        DMOD_LOG_ERROR("dmtty: node has no backing file configured\n");
        dmosi_mutex_lock(context->lock);
        slot->open_count--;
        dmosi_mutex_unlock(context->lock);
        return NULL;
    }

    /* Always open the backing file read-write: echo needs to write back to
     * it even when the caller only asked to read, and dmdevfs's own O_* flag
     * encoding does not line up 1:1 with DMDRVI_O_* (see docs/dmtty.md), so
     * trying to honor `flags` precisely here would be more misleading than
     * helpful. */
    void *backing_file = Dmod_FileOpen(slot->backing_path, "r+");
    if (backing_file == NULL)
    {
        DMOD_LOG_ERROR("dmtty: failed to open backing file '%s'\n", slot->backing_path);
        dmosi_mutex_lock(context->lock);
        slot->open_count--;
        dmosi_mutex_unlock(context->lock);
        return NULL;
    }

    dmtty_handle_t *handle = Dmod_Malloc(sizeof(dmtty_handle_t));
    if (handle == NULL)
    {
        Dmod_FileClose(backing_file);
        dmosi_mutex_lock(context->lock);
        slot->open_count--;
        dmosi_mutex_unlock(context->lock);
        return NULL;
    }
    memset(handle, 0, sizeof(*handle));
    handle->slot = slot;
    handle->backing_file = backing_file;

    (void)flags;
    return handle;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmtty, void, _close, ( dmdrvi_context_t context, void* handle ))
{
    if (!is_valid_context(context) || handle == NULL)
    {
        return;
    }

    dmtty_handle_t *h = (dmtty_handle_t *)handle;
    if (h->backing_file != NULL)
    {
        Dmod_FileClose(h->backing_file);
    }

    dmosi_mutex_lock(context->lock);
    if (h->slot->open_count > 0)
    {
        h->slot->open_count--;
    }
    dmosi_mutex_unlock(context->lock);

    Dmod_Free(handle);
}

dmod_dmdrvi_dif_api_declaration(1.0, dmtty, size_t, _read, ( dmdrvi_context_t context, void* handle, void* buffer, size_t size, uint32_t offset ))
{
    (void)offset;
    if (!is_valid_context(context) || handle == NULL || buffer == NULL || size == 0)
    {
        return 0;
    }

    dmtty_handle_t *h = (dmtty_handle_t *)handle;
    uint32_t flags = h->slot->flags;
    bool echo = (flags & dmtty_flag_echo) != 0;
    bool canonical = (flags & dmtty_flag_canonical) != 0;
    uint8_t *out = (uint8_t *)buffer;

    if (!canonical)
    {
        size_t n = Dmod_FileRead(out, 1, size, h->backing_file);
        if (echo && n > 0)
        {
            Dmod_FileWrite(out, 1, n, h->backing_file);
        }
        return n;
    }

    /* Canonical mode: assemble a full line into h->line_buf before handing
     * any of it to the caller, so a caller buffer smaller than the line can
     * still drain it across multiple _read() calls. */
    while (!h->have_line)
    {
        uint8_t byte;
        size_t n = Dmod_FileRead(&byte, 1, 1, h->backing_file);
        if (n == 0)
        {
            break; /* No more data available right now */
        }
        if (echo)
        {
            Dmod_FileWrite(&byte, 1, 1, h->backing_file);
        }
        h->line_buf[h->line_len++] = (char)byte;
        if (byte == '\n' || h->line_len >= sizeof(h->line_buf))
        {
            h->have_line = true;
        }
    }

    if (!h->have_line)
    {
        return 0; /* No complete line available yet */
    }

    size_t available = h->line_len - h->line_pos;
    size_t to_copy = (size < available) ? size : available;
    memcpy(out, h->line_buf + h->line_pos, to_copy);
    h->line_pos += to_copy;

    if (h->line_pos >= h->line_len)
    {
        h->line_len = 0;
        h->line_pos = 0;
        h->have_line = false;
    }

    return to_copy;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmtty, size_t, _write, ( dmdrvi_context_t context, void* handle, const void* buffer, size_t size, uint32_t offset ))
{
    (void)offset;
    if (!is_valid_context(context) || handle == NULL || buffer == NULL || size == 0)
    {
        return 0;
    }

    dmtty_handle_t *h = (dmtty_handle_t *)handle;
    return Dmod_FileWrite(buffer, 1, size, h->backing_file);
}

dmod_dmdrvi_dif_api_declaration(1.0, dmtty, int, _ioctl, ( dmdrvi_context_t context, void* handle, int command, void* arg ))
{
    if (!is_valid_context(context) || handle == NULL)
    {
        DMOD_LOG_ERROR("Invalid parameters in dmtty_dmdrvi_ioctl\n");
        return -EINVAL;
    }

    if (command <= 0 || command >= dmtty_ioctl_cmd_max)
    {
        DMOD_LOG_ERROR("dmtty: invalid ioctl command %d\n", command);
        return -EINVAL;
    }

    dmtty_handle_t *h = (dmtty_handle_t *)handle;
    dmtty_slot_t *slot = h->slot;

    switch (command)
    {
        case dmtty_ioctl_cmd_get_flags:
            if (arg == NULL) return -EINVAL;
            *(uint32_t *)arg = slot->flags;
            return 0;

        case dmtty_ioctl_cmd_set_flags:
            if (arg == NULL) return -EINVAL;
            slot->flags = *(uint32_t *)arg;
            return 0;

        case dmtty_ioctl_cmd_get_backing_path:
            if (arg == NULL) return -EINVAL;
            Dmod_SnPrintf((char *)arg, DMTTY_MAX_PATH_LEN + 1, "%s",
                          slot->backing_path != NULL ? slot->backing_path : "");
            return 0;

        case dmtty_ioctl_cmd_set_backing_path:
        {
            if (arg == NULL) return -EINVAL;
            const char *new_path = (const char *)arg;

            void *new_file = Dmod_FileOpen(new_path, "r+");
            if (new_file == NULL) return -ENODEV;

            char *new_backing_path = Dmod_StrDup(new_path);
            if (new_backing_path == NULL)
            {
                Dmod_FileClose(new_file);
                return -ENOMEM;
            }

            if (h->backing_file != NULL)
            {
                Dmod_FileClose(h->backing_file);
            }
            h->backing_file = new_file;
            h->line_len = 0;
            h->line_pos = 0;
            h->have_line = false;

            Dmod_Free(slot->backing_path);
            slot->backing_path = new_backing_path;
            return 0;
        }

        default:
            return -EINVAL;
    }
}

dmod_dmdrvi_dif_api_declaration(1.0, dmtty, int, _flush, ( dmdrvi_context_t context, void* handle ))
{
    if (!is_valid_context(context) || handle == NULL)
    {
        return -EINVAL;
    }
    /* Pure forwarding, no internal output buffering to flush. */
    return 0;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmtty, int, _stat, ( dmdrvi_context_t context, const char* path, dmdrvi_stat_t* stat ))
{
    (void)path;
    if (!is_valid_context(context) || stat == NULL)
    {
        DMOD_LOG_ERROR("Invalid parameters in dmtty_dmdrvi_stat\n");
        return -EINVAL;
    }

    stat->size = 0;    /* Stream device, no fixed size */
    stat->mode = 0666; /* Read-write permissions */
    return 0;
}
