/* ir.h - IR Blaster: ELK Smart / OCRUSTAR USB infrared dongles (learn and send remote-control codes).
 *
 * The device protocol is elksmart-comm by Přemysl Eric Janouch (0BSD), embedded in ir.c and spoken
 * through the kernel's usbfs (/dev/bus/usb/BBB/DDD ioctls), so SRS needs no libusb. The codes file is
 * IR Remote's (~/.config/irremote/remotes.json, the same format), shared with that app.
 *
 * Test hooks (never needed in normal use):
 *   SRS_TEST_IR=fake     a made-up dongle: nothing on USB is opened; every packet it would send is
 *                        appended to $SRS_TEST_IR_LOG ("OUT <hex>"), learning answers with canned data
 *                        ($SRS_TEST_IR_LEARN hex, or "never" = no answer); SRS_TEST_IR_PROTO=plain|huffman
 *                        picks its identify answer, SRS_TEST_IR_IDENT=none makes it not answer at all
 *   SRS_TEST_IR=noperm   a dongle that is there but may not be opened;  SRS_TEST_IR=absent  none
 *   SRS_TEST_IR_SYSFS=D  look for dongles in D instead of /sys/bus/usb/devices
 *   SRS_TEST_IR_RULES=D1:D2  look for the udev rule in these folders; SRS_TEST_IR_STREAM: the fake never stops
 *                        talking; SRS_TEST_IR_SHORT: the fake announces a longer learned code than it sends
 *   IRREMOTE_CONFIG=F    the codes file (like IR Remote); else $XDG_CONFIG_HOME/irremote/remotes.json */
#ifndef IR_H
#define IR_H

#include <stddef.h>

#define IR_VENDOR 0x045c
#define IR_SUPPORTED_IDS "045c:0132, 045c:0184, 045c:0195, 045c:0134"
#define IR_UDEV_NAME "70-elksmart-ir.rules"
#define IR_UDEV_RULE "/usr/lib/udev/rules.d/" IR_UDEV_NAME   /* the package's; a copy in /etc/udev/rules.d works the same */
#define IR_UDEV_LINE "SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"045c\", ATTRS{idProduct}==\"0132|0184|0195|0134\", TAG+=\"uaccess\""
#define IR_DEFAULT_FREQ 38000UL

/* ---- the dongle (detection reads sysfs only) ---- */
enum { IR_ABSENT, IR_NOPERM, IR_READY };
typedef struct {
    int state;           /* IR_ABSENT / IR_NOPERM / IR_READY */
    unsigned product;    /* 0x0132 ... */
    char node[48];       /* /dev/bus/usb/BBB/DDD */
    char sysdir[300];    /* /sys/bus/usb/devices/3-4 */
    char name[160];      /* "OCRUSTAR Smart IR Blaster" (manufacturer + product strings) */
    int fake;            /* SRS_TEST_IR made it up */
    int nonode;          /* IR_NOPERM because the device file is missing, not because of its permissions */
} ir_dongle;
int  ir_detect(ir_dongle *d);                                   /* returns d->state */
void ir_status_text(const ir_dongle *d, char *buf, size_t n);  /* one line: ready / the permission fix / not found */
const char *ir_absent_text(void);
/* the udev rule file in effect (/etc, /run, /usr/local/lib, /usr/lib or /lib udev/rules.d, first found), NULL when none */
const char *ir_udev_rule_found(char *buf, size_t n);                               /* "No ELK Smart / OCRUSTAR IR blaster found - plug one in." */

/* ---- device actions: these open the dongle (never from a section producer). 1 = ok; msg says what happened ---- */
int ir_probe(char *msg, size_t mn);                             /* identify only: no infrared is sent */
int ir_reset(char *msg, size_t mn);                             /* USB reset, then identify (un-sticks a dongle left in learn mode) */
/* send a code (hex) `repeat` times, 100 ms apart; nec: the code is NEC address+command pairs */
int ir_send(const char *hex, int nec, unsigned long freq, int repeat, volatile int *cancel, char *msg, size_t mn);
/* wait up to timeout_s for one remote button; code = hex of the learned command, hint = elksmart-comm's
   "attempted pulse decode" (may be empty) */
int ir_learn(double timeout_s, volatile int *cancel, char *code, size_t cn, char *hint, size_t hn, char *msg, size_t mn);
/* the packets a send would write, one "OUT <hex>" line each (malloc), for the protocol given (0 plain, 1 Huffman,
   -1 the one the dongle last answered with, plain if none); NULL + msg on a bad code */
char *ir_dry_run(const char *hex, int nec, unsigned long freq, int huffman, char *msg, size_t mn);
const char *ir_code_problem(const char *hex, int nec);          /* NULL when the code looks fine */
int ir_last_protocol(void);                                     /* -1 unknown, 0 plain, 1 Huffman */

/* ---- the codes file (IR Remote's remotes.json): a JSON tree, so fields SRS does not know are kept ---- */
typedef struct ir_doc ir_doc;
typedef struct { const char *name, *type, *code, *shortcut; unsigned long freq; } ir_button;
const char *ir_codes_path(void);
ir_doc *ir_doc_new(void);                                       /* { "version": 2, "remotes": [] } */
ir_doc *ir_doc_parse(const char *text, char *err, size_t en);   /* NULL = not an IR Remote file (err says why) */
/* NULL = the file is there but cannot be read or understood (err); a missing file gives an empty doc (*exists = 0) */
ir_doc *ir_doc_load(const char *path, int *exists, char *err, size_t en);
void ir_doc_free(ir_doc *d);
char *ir_doc_dump(const ir_doc *d);                             /* JSON text as IR Remote writes it (malloc) */
/* atomic write (path.tmp + rename); the first time SRS writes over an existing file it keeps a copy, path.before-srs */
int  ir_doc_save(ir_doc *d, const char *path, char *err, size_t en);
int  ir_doc_export(ir_doc *d, const char *path, char *err, size_t en);   /* the same, without the copy (Export) */
int  ir_nremotes(const ir_doc *d);
const char *ir_remote_name(const ir_doc *d, int r);
int  ir_nbuttons(const ir_doc *d, int r);
int  ir_button_at(const ir_doc *d, int r, int b, ir_button *out);
int  ir_find_remote(const ir_doc *d, const char *name);         /* exact name first, then ignoring case; -1 none */
int  ir_find_button(const ir_doc *d, int r, const char *name);
int  ir_remote_add(ir_doc *d, const char *name);                /* index; an existing remote of that name: its index */
int  ir_remote_rename(ir_doc *d, int r, const char *name);      /* 0 = empty name or taken */
int  ir_remote_delete(ir_doc *d, int r);
/* add the button or change its code (other fields such as shortcuts are kept); returns its index, -1 bad */
int  ir_button_set(ir_doc *d, int r, const char *name, const char *code, int nec, unsigned long freq);
int  ir_button_rename(ir_doc *d, int r, int b, const char *name);
int  ir_button_delete(ir_doc *d, int r, int b);
int  ir_doc_merge(ir_doc *base, const ir_doc *extra);           /* IR Remote's merge: add remotes, same-named buttons replaced */

/* the [ir-blaster] item */
struct srs_out;
void s_ir_blaster(struct srs_out *o);

#endif
