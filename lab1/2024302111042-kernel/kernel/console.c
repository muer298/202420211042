#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "course_sid.h"
#include "console.h"

#define UART_THR 0
#define UART_LSR 5
#define UART_LSR_THRE (1U << 5)

/* The interval is deliberately derived from COURSE_SID, never hard-coded. */
#define UART_THROTTLE_BYTES (16U + (COURSE_SID % 16U))

static uint uart_bytes_since_throttle;

void
consoleinit(void)
{
  /* QEMU's reset UART configuration suffices for polling-only output. */
  uart_bytes_since_throttle = 0;
}

static volatile uint8 *
uart_register(uint offset)
{
  return (volatile uint8 *)(UART0 + offset);
}

static void
throttle_if_needed(void)
{
  uint i;

  uart_bytes_since_throttle++;
  if (uart_bytes_since_throttle != UART_THROTTLE_BYTES)
    return;

  uart_bytes_since_throttle = 0;
  for (i = 0; i < UART_THROTTLE_BYTES; i++)
    asm volatile("nop");
}

/* Send exactly one hardware byte using the 16550 polling protocol. */
void
uartputc_sync(int c)
{
  while ((*uart_register(UART_LSR) & UART_LSR_THRE) == 0)
    ;

  *uart_register(UART_THR) = (uint8)c;
  io_fence();
  throttle_if_needed();
}

/* Apply the per-student presentation protocol above the raw UART layer. */
void
consputc(int c)
{
  uartputc_sync(c);
#if LAB1_BANNER_PROTOCOL == 1
  uartputc_sync('.');
#endif
}
