#include <stdarg.h>

#include "types.h"

#include "console.h"

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
    consputc(buf[--n]);
}

static void
print_signed(long value)
{
  uint64 magnitude;

  if (value < 0) {
    consputc('-');
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
    consputc(*s++);
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
      consputc(*fmt++);
      continue;
    }

    fmt++;
    if (*fmt == 'l') {
      is_long = 1;
      fmt++;
    }
    spec = *fmt;
    if (spec == '\0') {
      consputc('%');
      break;
    }
    fmt++;

    switch (spec) {
    case 'c':
      consputc(va_arg(ap, int));
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
      /* V2's %x includes the prefix, including for the value zero. */
      consputc('0');
      consputc('x');
      if (is_long)
        print_unsigned(va_arg(ap, unsigned long), 16);
      else
        print_unsigned(va_arg(ap, unsigned int), 16);
      break;
    case '%':
      consputc('%');
      break;
    default:
      consputc('%');
      if (is_long)
        consputc('l');
      consputc(spec);
      break;
    }
  }
  va_end(ap);
}
