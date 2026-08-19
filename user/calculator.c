#include "libc.h"
#include "stdlib.h"

#define LINE_MAX 256

static int readline(char *buf, int max) {
    int pos = 0;
    while (1) {
        char c;
        if (sys_read(STDIN_FILENO, &c, 1) != 1) continue;
        if (c == '\n') { putchar('\n'); buf[pos] = 0; return 0; }
        if (c == '\b' || c == 127) { if (pos > 0) { pos--; putchar('\b'); } continue; }
        if (c == 3) { puts("\n"); return -1; }
        if (c == 4) { puts("\n"); return -1; }
        if (pos < max - 1) { buf[pos++] = c; putchar(c); }
    }
}

static int parse_number(const char *s, int *out) {
    int neg = 0;
    int val = 0;
    int started = 0;

    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
        started = 1;
    }
    if (!started) return -1;
    *out = neg ? -val : val;
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    puts("EponaOS Calculator v1.0\n");
    puts("Enter expressions like: 2 + 3\n");
    puts("Operations: +, -, *, /, %%, quit\n\n");

    char line[LINE_MAX];

    while (1) {
        printf("calc> ");
        if (readline(line, LINE_MAX) < 0) break;
        if (line[0] == 0) continue;

        const char *p = line;

        /* Check for quit/exit */
        while (*p == ' ') p++;
        if (starts_with(p, "quit") || starts_with(p, "exit")) break;

        /* Parse: number op number */
        int a, b, result;
        if (parse_number(p, &a) < 0) {
            puts("Error: invalid first number\n");
            continue;
        }

        /* skip past first number */
        while (*p && *p >= '0' && *p <= '9') p++;
        while (*p == ' ') p++;

        char op = *p;
        if (!op) {
            puts("Error: expected operator (+, -, *, /, %%)\n");
            continue;
        }
        p++;

        if (parse_number(p, &b) < 0) {
            puts("Error: invalid second number\n");
            continue;
        }

        switch (op) {
            case '+': result = a + b; break;
            case '-': result = a - b; break;
            case '*': result = a * b; break;
            case '/':
                if (b == 0) { puts("Error: division by zero\n"); continue; }
                result = a / b;
                break;
            case '%':
                if (b == 0) { puts("Error: division by zero\n"); continue; }
                result = a % b;
                break;
            default:
                printf("Error: unknown operator '%c'\n", op);
                continue;
        }

        char buf[32];
        itoa(result, buf);
        puts(buf);
        putchar('\n');
    }

    puts("Goodbye!\n");
    return 0;
}
