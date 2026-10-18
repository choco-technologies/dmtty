/* Exercise the actual driver with a byte-limited stream backend. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/dmtty.c"

static unsigned char sent[4096];
static size_t sent_size, budget = sizeof(sent);
static size_t input_pos, input_size;
static const unsigned char *input;
static int locked;

static size_t send_bytes(const void *buffer, size_t size)
{
    assert(locked);
    if (size > budget) size = budget;
    assert(sent_size + size <= sizeof(sent));
    memcpy(sent + sent_size, buffer, size);
    sent_size += size;
    budget -= size;
    return size;
}

static size_t file_write(const void *buffer, size_t size, size_t count, void *file)
{
    assert(file == sent && size == 1);
    return send_bytes(buffer, count);
}

static size_t file_read(void *buffer, size_t size, size_t count, void *file)
{
    (void)file;
    assert(size == 1);
    if (count > input_size - input_pos) count = input_size - input_pos;
    memcpy(buffer, input + input_pos, count);
    input_pos += count;
    return count;
}

static int lock(dmosi_mutex_t mutex) { (void)mutex; assert(!locked); locked = 1; return 0; }
static int unlock(dmosi_mutex_t mutex) { (void)mutex; assert(locked); locked = 0; return 0; }
static void expect(const void *bytes, size_t size)
{
    assert(sent_size == size);
    assert(memcmp(sent, bytes, size) == 0);
    sent_size = 0;
    budget = sizeof(sent);
}

int main(void)
{
    Dmod_FileWrite = file_write;
    Dmod_WriteKernel = send_bytes;
    Dmod_FileRead = file_read;
    dmosi_mutex_lock = lock;
    dmosi_mutex_unlock = unlock;
    struct dmdrvi_context context = { .magic = DMTTY_CONTEXT_MAGIC };
    dmtty_slot_t slot = { .flags = DMTTY_FLAGS_DEFAULT, .onlcr = true };
    dmtty_handle_t h = { .slot = &slot, .backing_file = sent };
    dmtty_handle_t other = { .slot = &slot, .backing_file = sent };

    assert(dmdrvi_dmtty_write(&context, &h, "a\nb\n\n", 5, 0) == 5);
    expect("a\r\nb\r\n\r\n", 8);
    assert(handle_write(&h, "a\r\nb\r", 5) == 5);
    assert(handle_write(&other, "\n", 1) == 1);
    expect("a\r\nb\r\n", 6);

    /* A backend can stop after the inserted CR: retry only the unconsumed LF. */
    slot.output_cr = false;
    budget = 2;
    assert(handle_write(&h, "a\nb", 3) == 1);
    expect("a\r", 2);
    assert(handle_write(&other, "\nb", 2) == 2);
    expect("\nb", 2);
    budget = 0;
    assert(handle_write(&h, "\n", 1) == 0);
    expect("", 0);
    budget = 2;
    assert(handle_write(&h, "abcd\n", 5) == 2);
    expect("ab", 2);
    assert(handle_write(&h, "cd\n", 3) == 3);
    expect("cd\r\n", 4);
    assert(handle_write(&h, "", 0) == 0);
    expect("", 0);

    /* Raw input still needs CRLF output for shell line editing. */
    uint32_t flags = 0;
    assert(dmdrvi_dmtty_ioctl(&context, &h, dmtty_ioctl_cmd_set_flags, &flags) == 0);
    assert(handle_write(&h, "\n", 1) == 1);
    expect("\r\n", 2);
    /* Full binary output explicitly disables output processing too. */
    bool enabled = false;
    assert(dmdrvi_dmtty_ioctl(&context, &h, dmtty_ioctl_cmd_set_onlcr, &enabled) == 0);
    unsigned char binary[256];
    for (size_t i = 0; i < sizeof(binary); ++i) binary[i] = (unsigned char)i;
    assert(handle_write(&h, binary, sizeof(binary)) == sizeof(binary));
    expect(binary, sizeof(binary));
    enabled = true;
    assert(dmdrvi_dmtty_ioctl(&context, &h, dmtty_ioctl_cmd_set_onlcr, &enabled) == 0);
    flags = DMTTY_FLAGS_DEFAULT;
    assert(dmdrvi_dmtty_ioctl(&context, &h, dmtty_ioctl_cmd_set_flags, &flags) == 0);
    assert(handle_write(&h, "\n", 1) == 1);
    expect("\r\n", 2);

    enabled = false;
    assert(dmdrvi_dmtty_ioctl(&context, &h, dmtty_ioctl_cmd_set_onlcr, &enabled) == 0);
    assert(dmdrvi_dmtty_ioctl(&context, &other, dmtty_ioctl_cmd_get_onlcr, &enabled) == 0);
    assert(!enabled);
    assert(handle_write(&h, "\n", 1) == 1);
    expect("\n", 1);
    assert(dmdrvi_dmtty_ioctl(&context, &h, dmtty_ioctl_cmd_set_onlcr, NULL) == -EINVAL);
    enabled = true;
    assert(dmdrvi_dmtty_ioctl(&context, &h, dmtty_ioctl_cmd_set_onlcr, &enabled) == 0);

    /* The unbound kernel fallback applies exactly the same discipline. */
    h.backing_file = NULL;
    assert(handle_write(&h, "kernel\n", 7) == 7);
    expect("kernel\r\n", 8);
    h.backing_file = sent;

    /* Echo of CR, LF and CRLF must yield one terminal newline per line. */
    const unsigned char keys[] = "one\rtwo\nthree\r\n";
    input = keys;
    input_size = sizeof(keys) - 1;
    slot.foreground_handle = &h;
    char line[128];
    assert(dmdrvi_dmtty_read(&context, &h, line, sizeof(line), 0) == 4);
    assert(memcmp(line, "one\n", 4) == 0);
    assert(dmdrvi_dmtty_read(&context, &h, line, sizeof(line), 0) == 4);
    assert(dmdrvi_dmtty_read(&context, &h, line, sizeof(line), 0) == 6);
    assert(dmdrvi_dmtty_read(&context, &h, line, sizeof(line), 0) == 0);
    expect("one\r\ntwo\r\nthree\r\n", 17);
    puts("PASS: CRLF output, split writes/handles, short writes, raw binary, ioctls, kernel fallback and echo");
    return 0;
}
