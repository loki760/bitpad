/***    includes    ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

/***    defines    ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

enum editorKey // value 1000 to ensure no conflict with ordinary keypresses
{
    BACKSPACE = 127, // assigned ascii for better readabiity, actual rep is not readable
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/***    data    ***/
typedef struct erow // editor row
{
    int size;
    int rsize; // render size
    char *chars;
    char *render;
} erow;

struct editorConfig // terminal stats
{
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row; // array to store multi lines
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios;
};

struct editorConfig E;

/***    prototypes  ***/
void editorSetStatusMessage(const char *fmt, ...);

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

int editorReadKey()
{
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    if (c == '\x1b') // we are aliasing arrow keys to wsad
    {
        char seq[3]; // using 3 bytes

        if (read(STDIN_FILENO, &seq[0], 1) != 1) // read
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) // read
            return '\x1b';

        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '1':
                        return HOME_KEY; //<esc>[1~, <esc>[7~, <esc>[H, or <esc>OH
                    case '3':
                        return DEL_KEY; //<esc>[3~
                    case '4':
                        return END_KEY; //<esc>[4~, <esc>[8~, <esc>[F, or <esc>OF
                    case '5':
                        return PAGE_UP; //<esc>[5~
                    case '6':
                        return PAGE_DOWN; //<esc>[6~
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1]) // mapping arrow keys to wsad
                {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        }

        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }

        return '\x1b';
    }

    else // if time-out user just pressed esp only. return that
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

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) // TIOCGWINZ = Terminal IO Control Get WINdow siZe; write into ws
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

/***    row operations  ***/

int editorRowCxToRx(erow *row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++)
    {
        if (row->chars[j] == '\t')
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        rx++;
    }
    return rx;
}

void editorUpdateRow(erow *row) // copy chars in render string
{
    int tabs = 0;
    int j;

    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t')
            tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);

    int idx = 0;
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
        {
            row->render[idx++] = ' ';
            while (idx % KILO_TAB_STOP != 0)
                row->render[idx++] = ' ';
        }
        else
            row->render[idx++] = row->chars[j];
    }

    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len)
{
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); // multiply the number of bytes each erow takes and multiply that by the number of rows

    int at = E.numrows; // set the index to line no.
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;     // initialising rsize
    E.row[at].render = NULL; // initialising render
    editorUpdateRow(&E.row[at]);

    E.numrows++; // keep track of the no. of lines
    E.dirty++;   // tracking changes made, incrementing for quantitativity
}

void editorRowInsertChar(erow *row, int at, int c) // at is index at which char is inserted
{
    if (at < 0 || at > row->size)
        at = row->size;

    row->chars = realloc(row->chars, row->size + 2); // +2 for null byte too
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);

    row->size++;
    row->chars[at] = c;

    editorUpdateRow(row); // to update render and rsize
}

void editorRowDelChar(erow *row, int at)
{
    if (at < 0 || at >= row->size)
        return;

    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;

    editorUpdateRow(row);

    E.dirty++;
}

/***    editor operations   ***/

void editorInsertChar(int c)
{
    if (E.cy == E.numrows) // cursor is on ~ line after EOF, need to append new row before inserting character
    {
        editorAppendRow("", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

/***    file i/o    ***/

char *editorRowsToString(int *buflen)
{
    int totlen = 0;
    int j;

    for (j = 0; j < E.numrows; j++) // length of string to store row contents
        totlen += E.row[j].size + 1;

    *buflen = totlen;
    char *buf = malloc(totlen);
    char *p = buf;

    for (j = 0; j < E.numrows; j++) // storing the content user entered into p
    {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size; // pointer points to end of row
        *p = '\n';          // insert new line at end
        p++;                // pointer moved to start new row
    }

    return buf;
}

void editorOpen(char *filename)
{
    free(E.filename);
    E.filename = strdup(filename); // also allocates required amt of memory that u freed

    FILE *fp = fopen(filename, "r"); // open file in read mode
    if (!fp)
        die("fopen"); // error handling

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen; // signed size_t. it can return -1 when error

    while ((linelen = getline(&line, &linecap, fp)) != -1) // parse file line by line, getline returns -1 at EOF
    {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) // we're stripping carriage and newline cuz erow reps one line of text
            linelen--;
        editorAppendRow(line, linelen);
    }

    free(line);
    fclose(fp);
    E.dirty = 0; // initialising doesnt count as a change
}

void editorSave() // mapped to ctl+s
{
    if (E.filename == NULL)
        return;

    int len;
    char *buf = editorRowsToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);

    /*Saving users inputs to file he's provided*/
    if (fd != -1)
    {
        if (ftruncate(fd, len) != -1)
        {
            if (write(fd, buf, len) == len)
            {
                close(fd);
                free(buf);
                E.dirty = 0; // reseting count of changes
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }

    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
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
void editorScroll()
{
    E.rx = 0;
    if (E.cy < E.numrows)
    {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowoff)
    {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows)
    {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff)
    {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols)
    {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab) // draw ~ like vim
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows)
        {
            if (E.numrows == 0 && y == E.screenrows / 3)
            {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                                          "Kilo editor -- version %s", KILO_VERSION);
                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }
            else
            {
                abAppend(ab, "~", 1);
            }
        }
        else
        {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0)
                len = 0;
            if (len > E.screencols)
                len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[7m", 4); // esc seq that switches to inverted colours
    char status[80], rstatus[80];

    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                       E.filename ? E.filename : "[No Name]", E.numrows,
                       E.dirty ? "(modified)" : ""); // no name

    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows); // line no.

    if (len > E.screencols) // make sure name fits
        len = E.screencols;

    abAppend(ab, status, len);

    while (len < E.screencols) // insert whitespace until screen edge
    {
        if (E.screencols - len == rlen)
        {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3); // esc seq that switches back to normal formatting
    abAppend(ab, "\r\n", 2);   // space to display status message
}

void editorDrawMessageBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);

    if (msglen > E.screencols)
        msglen = E.screencols;

    if (msglen && time(NULL) - E.statusmsg_time < 5) // status msg will dissapear 5s after key press
        abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen()
{
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // reset mode
    /*abAppend(&ab, "\x1b[2J", 4);   // clear the terminal to the left side of the cursor. we hv optimized this by clearing line wise at each refresh*/
    abAppend(&ab, "\x1b[H", 3); // to position the cursor at the top left corner

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1); // terminal uses 1-indexed values, thus updated cs,cy
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // set mode

    write(STDOUT_FILENO, ab.b, ab.len); //\x1b==esc
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}
/***    input   ***/
void editorMoveCursor(int key)
{
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0)
            E.cx--;
        else if (E.cy > 0)
        {
            E.cy--;
            E.cx = E.row[E.cy].size;
        }
        break;
    case ARROW_RIGHT:
        if (row && E.cx < row->size)
            E.cx++;
        else if (row && E.cx == row->size)
        {
            E.cy++;
            E.cx = 0;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0)
            E.cy--;
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows)
            E.cy++;
        break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen)
    {
        E.cx = rowlen;
    }
}

void editorProcessKeypress()
{
    static int quit_times = KILO_QUIT_TIMES;
    int c = editorReadKey();

    switch (c)
    {

    case '\r':
        // TO DO
        break;

    case CTRL_KEY('q'):
        if (E.dirty && quit_times > 0) // to quit with unsaved changes, 3 ctl+q
        {
            editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                                   "Press Ctrl-Q %d more times to quit.",
                                   quit_times);
            quit_times--;
            return;
        }

        write(STDOUT_FILENO, "\x1b[2J", 4); // clear and reposition cursor upon exit
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case CTRL_KEY('s'):
        editorSave();
        break;

    case HOME_KEY:
        E.cx = 0;
        break;
    case END_KEY:
        if (E.cy < E.numrows)
            E.cx = E.row[E.cy].size;
        break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        /* TODO */
        break;

    case PAGE_UP:
    case PAGE_DOWN:
    {
        if (c == PAGE_DOWN)
        {
            E.cy = E.rowoff; // simulate the whole pages worth of scrolls
        }
        else if (c == PAGE_DOWN)
        {
            E.cy = E.rowoff + E.screenrows - 1;
            if (E.cy > E.numrows)
                E.cy = E.numrows;
        }

        int times = E.screenrows;
        while (times--)
        {
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN); // call move cursor reqd times using ternary
        }
        break;
    }

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c); // function that uses wsad to move cursor around
        break;

    case CTRL_KEY('l'):
    case '\x1b': // a redundancy for accidental esc sequences (they are being ignored)
        break;

    default:
        editorInsertChar(c); // for text editing as default
        break;
    }

    quit_times = KILO_QUIT_TIMES; // if any other key press is encountered, reset.
}

/***    init    ***/
void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.numrows = 0;
    E.rowoff = 0; // row
    E.coloff = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.dirty = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");

    E.screenrows -= 2; // status bar, status msg
}

int main(int argc, char *argv[]) // argument count, argument vector(array of strings)
{
    enableRawMode();
    initEditor();
    if (argc >= 2) // pass filename to view it after ./kilo
    {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctl+S = save | Ctl+Q = quit");

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}