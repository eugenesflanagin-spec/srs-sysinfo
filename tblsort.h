/* tblsort.h - unit-aware ordering of table cells for the sortable tables of the main window.
 * Plain C, no GTK: numbers with their units (RPM, KB/MB/GB/TB, Hz..GHz, °C, %, V, W, s ...),
 * dates (YYYY-MM-DD, MM/DD/YYYY), times (HH:MM:SS), [123]-style bracketed numbers and 0x hex
 * sort by value; everything else sorts as text with numbers inside it compared as numbers
 * ("CPU-2" before "CPU-10"). Empty cells and "-" always sort last. */
#ifndef TBLSORT_H
#define TBLSORT_H

/* <0, 0, >0 for ascending order (the caller reverses for descending, except empty/"-" which it
   keeps last: ts_missing() tells which cells those are) */
int ts_cmp(const char *a, const char *b);
int ts_missing(const char *s);
/* the value a cell sorts by, when it is a number / date / time: 1 and *v set, else 0 */
int ts_value(const char *s, double *v);

#endif
