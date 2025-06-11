#include <stdio.h>
#include <unistd.h>
#include <termios.h>

int main() {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    cfmakeraw(&newt);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    int c = getchar();  // Waits for one keypress
    printf("You pressed: ASCII %d\n", c);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return 0;
}
