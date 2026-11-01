/* gui_ir.h - the IR Blaster tab's Remotes view (srs-gui main window, USB & PCI devices page) */
#ifndef GUI_IR_H
#define GUI_IR_H

#include <gtk/gtk.h>

/* Status of the ELK Smart / OCRUSTAR dongle (checked every 2 s and when shown: plug in / out is seen), the remotes and
 * codes of IR Remote's remotes.json, Learn (countdown, Cancel, then name and save), Send (repeat count, Dry run shows
 * the USB packets only), Type Code, rename / delete, Import / Export, Open IR Remote. on_state (may be NULL) is called
 * with IR_ABSENT / IR_NOPERM / IR_READY when the dongle comes or goes, so the tab can be greyed. */
GtkWidget *ir_manager_new(void (*on_state)(int state, gpointer data), gpointer data);

#endif
