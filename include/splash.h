#ifndef SPLASH_H
#define SPLASH_H
void splash_init(void);
void splash_step(const char *msg, int pct, void (*fn)(void));  /* roda fn e anima ate pct% */
void splash_end(void);
#endif
