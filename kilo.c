/***    includes    ***/
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>

/***    defines    ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/***    data    ***/
struct editorConfig
{
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/***    terminal    ***/
// error handling
void die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4); // to ensure we dont get garbage over terminal if error in rendering
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

// to be kind to the user. Resets terminal attributes
void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios; // making a copy of the original attributes of the terminal

    /*c_lfalg is input behaviour control
    bitwise and and not operator on ECHO which sets all its bytes to 0. turned ECHO off
    ctrl C: terminate --> ISIG
    ctrl Z: suspend --> ISIG
    ctrl S: stop data transfer to the terminal --> IXON
    ctrl Q: resume " --> IXON
    ctrl V: --> IEXTEN
    \r\n --> OPOST*/
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // input flag
    raw.c_oflag &= ~(OPOST);                                  // output flag, disabling \r
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cflag |= ~(CS8); // A bit mask

    /*vmin, vtime are controle characters
    vmin: min num of bits read needs before returning (it will return as soon as there's input thus)
    vtime: timeout value (its in tenths of a second, i.e, we've set it to 0.01s)*/
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

char editorReadKey()
{
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    return c;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        return -1;
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
/***    output  ***/
void editorDrawRows()
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editorRefreshScreen()
{
    write(STDOUT_FILENO, "\x1b[2J", 4); // clear the terminal to the left side of the cursor
    write(STDOUT_FILENO, "\x1b[H", 3);  // to position the cursor at the top left corner

    editorDrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3); //\x1b==esc
}

/***    input   ***/
void editorProcessKeypress()
{
    char c = editorReadKey();

    switch (c)
    {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4); // clear and reposition cursor upon exit
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    }
}

/***    init    ***/
void initEditor()
{
    if (getWindowSize(&E.screencols, &E.screenrows) == -1)
        die("getWindowSize");
}

int main()
{
    enableRawMode();

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}