#include "lab3.h"
void *memset(void *dst, int c, uint64 n) {
  uchar *d = dst;
  while(n--) *d++ = c;
  return dst;
}
void *memcpy(void *dst, const void *src, uint64 n) {
  uchar *d = dst;
  const uchar *s = src;
  while(n--) *d++ = *s++;
  return dst;
}
int streq(const char *a, const char *b) {
  while(*a && *a == *b) { a++; b++; }
  return *a == *b;
}
void panic(const char *why) {
  intr_off();
  console_plain();
  printf("\nPANIC: %s\n", why);
  for(;;) asm volatile("wfi");
}
