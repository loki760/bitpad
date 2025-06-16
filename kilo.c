/***    includes    ***/
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

/***    defines    ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"

/***    data    ***/
struct editorConfig // terminal stats
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

int getCursorPosition(int *rows, int *cols) // fallback protocol to get terminal window size
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) // error handling
        return -1;

    while (i < sizeof(buf) - 1) // getting the escape sequence for size of terminal window
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }

    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') // error handling
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) // parsed the escape sequence returned to rows and cols
        return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws; //

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) // TIOCGWINZ = Terminal IO Control Get WINdow siZe; write isnto ws
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1; // sending the cursor to the bottom-right corner (right then down); 999 ensures ends
        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        // printf("Detected rows: %d, cols: %d\r\n", E.screenrows, E.screencols);
        return 0;
    }
}
/***    append buffer   ***/
struct abuf
{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) // append all write fncs to buffer
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;

    memcpy(&new[ab->len], s, len);

    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) // destructor that deallocates dyn mem used by abuf
{
    free(ab->b);
}

/***    output  ***/
void editorDrawRows(struct abuf *ab) // draw ~ like vim
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        if (y == E.screenrows / 3)
        {
            char welcome[80];

            int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION); // printing the ver of kilo

            if (welcomelen > E.screencols) // truncate len if terminal too small
                welcomelen = E.screencols;

            int padding = (E.screencols - welcomelen) / 2; // centering the msg
            if (padding)
            {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding--) // printing it in the centre
                abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomelen);
        }

        else
        {
            abAppend(ab, "~", 1); // split the process into 3 bytes (~,\r,\n)
        }

        abAppend(ab, "\x1b[K", 3); // erase in line command. 0 is deafult. it will clear to the right of the cursor
        if (y < E.screenrows - 1)  // this will ensure terminal doesnt do the last \r\n.
        {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen()
{
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // reset mode
    /*abAppend(&ab, "\x1b[2J", 4);   // clear the terminal to the left side of the cursor. we hv optimized this by clearing line wise at each refresh*/
    abAppend(&ab, "\x1b[H", 3); // to position the cursor at the top left corner

    editorDrawRows(&ab);

    abAppend(&ab, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25h", 6); // set mode

    write(STDOUT_FILENO, ab.b, ab.len); //\x1b==esc
    abFree(&ab);
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
    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    {
        die("getWindowSize");
    }
}

int main()
{
    enableRawMode();
    initEditor();

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}