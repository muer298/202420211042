#ifndef LAB1_CONSOLE_H
#define LAB1_CONSOLE_H

void consoleinit(void);
void uartputc_sync(int c);
void consputc(int c);
void printf(const char *fmt, ...);

#endif
