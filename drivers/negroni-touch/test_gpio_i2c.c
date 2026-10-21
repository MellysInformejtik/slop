#include <assert.h>
#include <string.h>
#include "gpio_i2c.h"
struct fake {
    int sda, scl, stuck;
    const int *samples;
    unsigned count, pos, rises, waits;
    int wire[128];
};
static void sda(void *p, int v) { ((struct fake *)p)->sda = v; }
static void scl(void *p, int v) {
    struct fake *f = p;
    f->scl = v;
    if (v && f->rises < 128) f->wire[f->rises++] = f->sda;
}
static int get_sda(void *p) {
    struct fake *f = p;
    assert(f->pos < f->count);
    return f->samples[f->pos++];
}
static int get_scl(void *p) { struct fake *f = p; f->waits++; return !f->stuck; }
static void delay(unsigned us) { assert(us == 1 || us == 5); }
static void byte_at(struct fake *f, unsigned at, unsigned byte) {
    for (unsigned i = 0; i < 8; i++) assert(f->wire[at+i] == !!(byte & (0x80 >> i)));
}
int main(void) {
    const int acks[] = {1, 0, 0};
    struct fake f = {.samples=acks, .count=3};
    struct gpio_i2c g = {&f, sda, scl, get_sda, get_scl, delay};
    uint8_t b = 0x25;
    assert(gpio_i2c_write(&g, 0x4b, &b, 1) == 0);
    byte_at(&f, 1, 0x96); byte_at(&f, 10, 0x25);
    assert(f.pos == 3 && f.sda == 1 && f.scl == 1);
    const int input[] = {1, 0, 1,0,1,0,0,1,0,1};
    f = (struct fake){.samples=input, .count=10};
    assert(gpio_i2c_read(&g, 0x4b, &b, 1) == 0 && b == 0xa5);
    byte_at(&f, 1, 0x97);
    assert(f.wire[18] == 1); /* final byte NACK */
    const int two[] = {1, 0, 1,0,1,0,0,1,0,1, 0,1,0,1,1,0,1,0};
    uint8_t pair[2];
    f = (struct fake){.samples=two, .count=18};
    assert(gpio_i2c_read(&g, 0x4b, pair, 2) == 0);
    assert(pair[0] == 0xa5 && pair[1] == 0x5a);
    assert(f.wire[18] == 0 && f.wire[27] == 1); /* ACK then NACK */
    const int nack[] = {1, 1};
    f = (struct fake){.samples=nack, .count=2};
    assert(gpio_i2c_read(&g, 0x4b, &b, 1) == -2);
    f = (struct fake){.stuck=1};
    assert(gpio_i2c_read(&g, 0x4b, &b, 1) == -4);
    assert(f.waits == 2000); /* bounded start and STOP clock waits */
    const int busy[] = {0};
    f = (struct fake){.samples=busy, .count=1};
    assert(gpio_i2c_read(&g, 0x4b, &b, 1) == -3);
    return 0;
}
