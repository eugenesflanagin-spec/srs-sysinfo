/* tests/ir_fake_libusb.c - a stand-in libusb-1.0.so.0 for running the real /usr/bin/elksmart-comm against a
 * made-up ELK Smart dongle (045c:0132), so tests/ir-test.sh can compare the USB packets elksmart-comm writes with
 * the ones SRS's embedded protocol writes, byte for byte. Nothing here touches USB: there is one fake device, every
 * bulk OUT transfer is appended to $SRS_TEST_IR_LOG as "OUT <hex>", and the IN side answers like SRS's own fake
 * (SRS_TEST_IR=fake): identify -> fc fc fc fc 70 01 (or 02 aa with SRS_TEST_IR_PROTO=huffman); learn -> the 0132
 * acknowledgement ff ff ff ff, a pause, fe fe fe fe + size + the code in $SRS_TEST_IR_LEARN (hex), in pieces.
 * Built by the test as libusb-1.0.so.0 and found through LD_LIBRARY_PATH; the test checks the loader picks it. */
#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct { unsigned char d[64]; int n, timeout; } q[64];
static int qh, qn;
static int fake_dev, fake_handle;

static void push(const unsigned char *d, int n, int timeout)
{
    if (qn >= 64) return;
    int i = (qh + qn++) % 64;
    q[i].n = n;
    q[i].timeout = timeout;
    if (n) memcpy(q[i].d, d, (size_t)n);
}

static void logpkt(const unsigned char *b, int n)
{
    const char *p = getenv("SRS_TEST_IR_LOG");
    FILE *f = p ? fopen(p, "a") : NULL;
    if (!f) return;
    fputs("OUT ", f);
    for (int i = 0; i < n; i++) fprintf(f, "%02x", b[i]);
    fputc('\n', f);
    fclose(f);
}

static int unhex(const char *s, unsigned char *out, int max)
{
    int n = 0;
    while (s[0] && s[1] && n < max) { unsigned v; sscanf(s, "%2x", &v); out[n++] = (unsigned char)v; s += 2; }
    return n;
}

static void on_out(const unsigned char *b, int n)
{
    static const unsigned char idf[4] = { 0xfc, 0xfc, 0xfc, 0xfc }, lrn[4] = { 0xfe, 0xfe, 0xfe, 0xfe };
    logpkt(b, n);
    if (n == 4 && !memcmp(b, idf, 4)) {
        const char *pr = getenv("SRS_TEST_IR_PROTO");
        unsigned char r[6] = { 0xfc, 0xfc, 0xfc, 0xfc, 0x70, 0x01 };
        if (pr && !strcmp(pr, "huffman")) { r[4] = 0x02; r[5] = 0xaa; }
        push(r, 6, 0);
    } else if (n == 4 && !memcmp(b, lrn, 4)) {
        static const unsigned char ack[4] = { 0xff, 0xff, 0xff, 0xff };
        static unsigned char code[8192];
        const char *hx = getenv("SRS_TEST_IR_LEARN");
        int len = hx ? unhex(hx, code, sizeof code) : 0, at;
        unsigned char first[64];
        push(ack, 4, 0);
        push(NULL, 0, 1);
        memcpy(first, lrn, 4);
        first[4] = (unsigned char)(len >> 8);
        first[5] = (unsigned char)len;
        at = len < 58 ? len : 58;
        memcpy(first + 6, code, (size_t)at);
        push(first, 6 + at, 0);
        push(NULL, 0, 1);
        while (at < len) { int k = len - at < 64 ? len - at : 64; push(code + at, k, 0); at += k; }
    }
}

int LIBUSB_CALL libusb_init_context(libusb_context **ctx, const struct libusb_init_option options[], int num_options)
{ (void)options; (void)num_options; if (ctx) *ctx = NULL; return 0; }
int LIBUSB_CALL libusb_init(libusb_context **ctx) { if (ctx) *ctx = NULL; return 0; }
void LIBUSB_CALL libusb_exit(libusb_context *ctx) { (void)ctx; }

ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context *ctx, libusb_device ***list)
{
    (void)ctx;
    *list = calloc(2, sizeof **list);
    (*list)[0] = (libusb_device *)&fake_dev;
    return 1;
}
void LIBUSB_CALL libusb_free_device_list(libusb_device **list, int unref) { (void)unref; free(list); }

int LIBUSB_CALL libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc)
{
    (void)dev;
    memset(desc, 0, sizeof *desc);
    desc->bLength = LIBUSB_DT_DEVICE_SIZE;
    desc->bDescriptorType = LIBUSB_DT_DEVICE;
    desc->idVendor = 0x045c;
    desc->idProduct = 0x0132;
    desc->bNumConfigurations = 1;
    return 0;
}

int LIBUSB_CALL libusb_open(libusb_device *dev, libusb_device_handle **h) { (void)dev; *h = (libusb_device_handle *)&fake_handle; return 0; }
void LIBUSB_CALL libusb_close(libusb_device_handle *h) { (void)h; }
libusb_device *LIBUSB_CALL libusb_get_device(libusb_device_handle *h) { (void)h; return (libusb_device *)&fake_dev; }
int LIBUSB_CALL libusb_kernel_driver_active(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
int LIBUSB_CALL libusb_detach_kernel_driver(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
int LIBUSB_CALL libusb_claim_interface(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
int LIBUSB_CALL libusb_release_interface(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }

static struct libusb_endpoint_descriptor eps[2] = {
    { .bLength = 7, .bDescriptorType = LIBUSB_DT_ENDPOINT, .bEndpointAddress = 0x01, .bmAttributes = LIBUSB_TRANSFER_TYPE_BULK, .wMaxPacketSize = 64 },
    { .bLength = 7, .bDescriptorType = LIBUSB_DT_ENDPOINT, .bEndpointAddress = 0x81, .bmAttributes = LIBUSB_TRANSFER_TYPE_BULK, .wMaxPacketSize = 64 },
};
static struct libusb_interface_descriptor alt = {
    .bLength = 9, .bDescriptorType = LIBUSB_DT_INTERFACE, .bNumEndpoints = 2, .bInterfaceClass = LIBUSB_CLASS_VENDOR_SPEC, .endpoint = eps,
};
static struct libusb_interface iface = { .altsetting = &alt, .num_altsetting = 1 };
static struct libusb_config_descriptor cfg = {
    .bLength = 9, .bDescriptorType = LIBUSB_DT_CONFIG, .bNumInterfaces = 1, .bConfigurationValue = 1, .interface = &iface,
};

int LIBUSB_CALL libusb_get_active_config_descriptor(libusb_device *dev, struct libusb_config_descriptor **config)
{ (void)dev; *config = &cfg; return 0; }
void LIBUSB_CALL libusb_free_config_descriptor(struct libusb_config_descriptor *config) { (void)config; }

int LIBUSB_CALL libusb_bulk_transfer(libusb_device_handle *h, unsigned char ep, unsigned char *data, int length, int *transferred, unsigned int timeout)
{
    (void)h; (void)timeout;
    *transferred = 0;
    if (!(ep & 0x80)) { on_out(data, length); *transferred = length; return 0; }
    if (!qn) return LIBUSB_ERROR_TIMEOUT;
    int i = qh;
    qh = (qh + 1) % 64;
    qn--;
    if (q[i].timeout) return LIBUSB_ERROR_TIMEOUT;
    int n = q[i].n < length ? q[i].n : length;
    memcpy(data, q[i].d, (size_t)n);
    *transferred = n;
    return 0;
}

const char *LIBUSB_CALL libusb_strerror(int code) { return code == LIBUSB_ERROR_TIMEOUT ? "Operation timed out" : "fake libusb error"; }
