#include <stdarg.h>

#include "types.h"

extern void consoleputc(int c);

static void
print_unsigned(uint64 value, uint base)
{
  static const char digits[] = "0123456789abcdef";
  char buf[32];
  uint n = 0;

  do {
    buf[n++] = digits[value % base];
    value /= base;
  } while (value != 0);

  while (n != 0)
    consoleputc(buf[--n]);
}

static void
print_signed(long value)
{
  uint64 magnitude;

  if (value < 0) {
    consoleputc('-');
    /* This form also works for the most-negative signed value. */
    magnitude = (uint64)(-(value + 1)) + 1;
  } else {
    magnitude = (uint64)value;
  }
  print_unsigned(magnitude, 10);
}

static void
print_string(const char *s)
{
  if (s == 0)
    s = "(null)";
  while (*s != '\0')
    consoleputc(*s++);
}

/* Minimal freestanding printf: %c, %s, %d, %u, %x and long forms. */
void
printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  while (*fmt != '\0') {
    int is_long = 0;
    char spec;

    if (*fmt != '%') {
      consoleputc(*fmt++);
      continue;
    }

    fmt++;
    if (*fmt == 'l') {
      is_long = 1;
      fmt++;
    }
    spec = *fmt;
    if (spec == '\0') {
      consoleputc('%');
      break;
    }
    fmt++;

    switch (spec) {
    case 'c':
      consoleputc(va_arg(ap, int));
      break;
    case 's':
      print_string(va_arg(ap, const char *));
      break;
    case 'd':
      if (is_long)
        print_signed(va_arg(ap, long));
      else
        print_signed(va_arg(ap, int));
      break;
    case 'u':
      if (is_long)
        print_unsigned(va_arg(ap, unsigned long), 10);
      else
        print_unsigned(va_arg(ap, unsigned int), 10);
      break;
    case 'x':
      if (is_long)
        print_unsigned(va_arg(ap, unsigned long), 16);
      else
        print_unsigned(va_arg(ap, unsigned int), 16);
      break;
    case '%':
      consoleputc('%');
      break;
    default:
      consoleputc('%');
      if (is_long)
        consoleputc('l');
      consoleputc(spec);
      break;
    }
  }
  va_end(ap);
}
