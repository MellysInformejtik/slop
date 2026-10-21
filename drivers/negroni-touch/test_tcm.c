/* Host regression checks: cc -std=c11 -Wall -Wextra -Werror test_tcm.c -o /tmp/test_tcm */
#include <assert.h>
#include <string.h>
#include "syna_tcm.h"

struct packet { const uint8_t *bytes; uint32_t len; int rc; };
struct fake { const struct packet *packets; unsigned count, next, writes; };
static int read_packet(void *ctx, uint8_t *buf, uint32_t len)
{
    struct fake *f = ctx;
    assert(f->next < f->count);
    const struct packet *p = &f->packets[f->next++];
    assert(p->len == len);
    if (!p->rc) memcpy(buf, p->bytes, len);
    return p->rc;
}
static int write_packet(void *ctx, const uint8_t *buf, uint32_t len)
{
    struct fake *f = ctx;
    assert(len == 3 && buf[0] == TCM_CMD_GET_TOUCH_REPORT_CONFIG);
    f->writes++;
    return 0;
}
static void wait_tick(void) {}
int main(void)
{
    const uint8_t idle[] = {0xa5, 0, 0xff, 0xff};
    const uint8_t busy[] = {0xa5, 2, 0xff, 0xff};
    const uint8_t identify[] = {0xa5, 0x10, 2, 0};
    const uint8_t identify_data[] = {0xa5, 3, 1, 1, 0x5a};
    const uint8_t ok[] = {0xa5, 1, 2, 0};
    const uint8_t config[] = {0xa5, 3, 0, 0, 0x5a};
    const struct packet packets[] = {
        {idle, 4, 0}, {busy, 4, 0}, {identify, 4, 0},
        {identify_data, 5, 0}, {ok, 4, 0}, {config, 5, 0}
    };
    struct fake f = {packets, 6, 0, 0};
    struct tcm t = {0};
    t.io = (struct tcm_io){&f, read_packet, write_packet};
    assert(tcm_get_report_config(&t, wait_tick) == 0);
    assert(t.config_len == 2 && f.next == 6 && f.writes == 1);

    const struct packet failed[] = {{0, 4, -4}};
    f = (struct fake){failed, 1, 0, 0};
    assert(tcm_read_message(&t) == -1 && t.transport_error == -4);
    assert(t.payload_len == 0 && t.code == 0);

    const uint8_t bad[] = {0xa5, 3, 0, 0, 0};
    const struct packet corrupt[] = {{ok, 4, 0}, {bad, 5, 0}};
    f = (struct fake){corrupt, 2, 0, 0};
    assert(tcm_read_message(&t) == -3 && t.payload_len == 0);

    const uint8_t zero[] = {0, 0, 0, 0};
    const struct packet empty[] = {{zero, 4, 0}};
    f = (struct fake){empty, 1, 0, 0};
    assert(tcm_read_message(&t) == 1 && t.header[0] != TCM_MARKER);
    return 0;
}
