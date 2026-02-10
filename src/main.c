/*
 * Paperleaf — minimal e-reader shell for Raspberry Pi (Linux console + ncurses).
 * Main menu: Library | Power off
 * Library: PDF list + Back; Enter opens the selected PDF (external viewer).
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <curses.h>

#define MAX_PDFS 256
#define PDF_NAME_MAX 256

typedef enum {
    SCREEN_MAIN,
    SCREEN_LIBRARY,
} Screen;

static char library_dir[PATH_MAX];
static char pdf_viewer[128];
static char pdf_paths[MAX_PDFS][PATH_MAX];
static char pdf_names[MAX_PDFS][PDF_NAME_MAX];
static size_t pdf_count;

static bool ends_with_ci(const char *name, const char *ext)
{
    size_t nl = strlen(name);
    size_t el = strlen(ext);
    if (nl < el)
        return false;
    for (size_t i = 0; i < el; i++) {
        char a = (char)tolower((unsigned char)name[nl - el + i]);
        char b = (char)tolower((unsigned char)ext[i]);
        if (a != b)
            return false;
    }
    return true;
}

static void scan_library(void)
{
    pdf_count = 0;
    DIR *d = opendir(library_dir);
    if (!d)
        return;

    struct dirent *e;
    while ((e = readdir(d)) != NULL && pdf_count < MAX_PDFS) {
        if (e->d_name[0] == '.' && (e->d_name[1] == '\0' ||
                                    (e->d_name[1] == '.' && e->d_name[2] == '\0')))
            continue;
        if (!ends_with_ci(e->d_name, ".pdf"))
            continue;

        char full[PATH_MAX];
        int n = snprintf(full, sizeof full, "%s/%s", library_dir, e->d_name);
        if (n < 0 || (size_t)n >= sizeof full)
            continue;

        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode))
            continue;

        strncpy(pdf_names[pdf_count], e->d_name, PDF_NAME_MAX - 1);
        pdf_names[pdf_count][PDF_NAME_MAX - 1] = '\0';
        strncpy(pdf_paths[pdf_count], full, PATH_MAX - 1);
        pdf_paths[pdf_count][PATH_MAX - 1] = '\0';
        pdf_count++;
    }
    closedir(d);

    if (pdf_count > 1) {
        /* Sort names; keep paths aligned via parallel arrays using stable index sort */
        size_t idx[MAX_PDFS];
        for (size_t i = 0; i < pdf_count; i++)
            idx[i] = i;
        /* simple bubble by name via idx */
        for (size_t i = 0; i + 1 < pdf_count; i++) {
            for (size_t j = 0; j + 1 < pdf_count - i; j++) {
                if (strcasecmp(pdf_names[idx[j]], pdf_names[idx[j + 1]]) > 0) {
                    size_t t = idx[j];
                    idx[j] = idx[j + 1];
                    idx[j + 1] = t;
                }
            }
        }
        char tmpn[MAX_PDFS][PDF_NAME_MAX];
        char tmpp[MAX_PDFS][PATH_MAX];
        for (size_t i = 0; i < pdf_count; i++) {
            strncpy(tmpn[i], pdf_names[idx[i]], PDF_NAME_MAX);
            strncpy(tmpp[i], pdf_paths[idx[i]], PATH_MAX);
        }
        for (size_t i = 0; i < pdf_count; i++) {
            strncpy(pdf_names[i], tmpn[i], PDF_NAME_MAX);
            strncpy(pdf_paths[i], tmpp[i], PATH_MAX);
        }
    }
}

static void load_config_from_env(void)
{
    const char *lib = getenv("PAPERLEAF_LIBRARY");
    if (lib && lib[0]) {
        if (realpath(lib, library_dir) == NULL)
            strncpy(library_dir, lib, sizeof library_dir - 1);
    } else {
        if (realpath("library", library_dir) == NULL)
            strncpy(library_dir, ".", sizeof library_dir - 1);
    }
    library_dir[sizeof library_dir - 1] = '\0';

    const char *viewer = getenv("PAPERLEAF_PDF_VIEWER");
    if (viewer && viewer[0]) {
        strncpy(pdf_viewer, viewer, sizeof pdf_viewer - 1);
        pdf_viewer[sizeof pdf_viewer - 1] = '\0';
    } else {
        strncpy(pdf_viewer, "mupdf", sizeof pdf_viewer - 1);
    }
}

static void suspend_curses(void)
{
    def_prog_mode();
    endwin();
    fflush(stdout);
}

static void resume_curses(void)
{
    refresh();
}

static void open_pdf(const char *path)
{
    suspend_curses();

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        resume_curses();
        return;
    }
    if (pid == 0) {
        /* Child: replace with viewer; common viewers accept file as last arg */
        execlp(pdf_viewer, pdf_viewer, path, (char *)NULL);
        _exit(127);
    }

    int st = 0;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            break;
        }
    }

    resume_curses();
}

static void try_poweroff(void)
{
    endwin();
    fflush(stdout);
    /* Prefer systemctl when available (no full path needed on Pi OS) */
    if (execlp("systemctl", "systemctl", "poweroff", (char *)NULL) == -1)
        if (execlp("shutdown", "shutdown", "-h", "now", (char *)NULL) == -1)
            if (execlp("poweroff", "poweroff", (char *)NULL) == -1) {
                fprintf(stderr,
                        "Could not run poweroff (install polkit rule or: sudo ./paperleaf).\n");
                exit(1);
            }
}

static void draw_main_menu(int sel)
{
    erase();
    mvprintw(1, 2, "Paperleaf");
    mvprintw(3, 2, "> Library (collection of PDF files)");
    mvprintw(4, 2, "> Power off");

    int row = 3 + sel;
    mvchgat(row, 0, -1, A_REVERSE, 0, NULL);

    mvprintw(LINES - 2, 2, "Up/Down: move   Enter: choose   Q: quit");
    refresh();
}

static void draw_library(int sel)
{
    erase();
    mvprintw(1, 2, "Library — %s", library_dir);
    mvprintw(2, 2, "Open: highlight a PDF and press Enter   Back: last item");

    int row = 4;
    const int max_rows = LINES - 6;
    size_t first = 0;

    if (pdf_count > 0 && sel >= max_rows)
        first = (size_t)(sel - max_rows + 1);

    for (size_t i = first; i < pdf_count && row < LINES - 4; i++, row++) {
        mvprintw(row, 4, "%s", pdf_names[i]);
    }

    mvprintw(row, 4, "Back (goes back to main menu)");
    int back_row = row;

    /* Highlight: either a PDF row or Back */
    int hl_row;
    if (sel < (int)pdf_count)
        hl_row = 4 + sel - (int)first;
    else
        hl_row = back_row;

    if (hl_row >= 4 && hl_row <= back_row)
        mvchgat(hl_row, 0, -1, A_REVERSE, 0, NULL);

    if (pdf_count == 0)
        mvprintw(4, 4, "(no PDF files found — add .pdf to this folder)");

    mvprintw(LINES - 2, 2, "Up/Down: move   Enter: open / go back");
    refresh();
}

int main(void)
{
    load_config_from_env();
    scan_library();

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    if (has_colors())
        start_color();

    Screen screen = SCREEN_MAIN;
    int main_sel = 0;     /* 0 = Library, 1 = Power off */
    int library_sel = 0;  /* 0..pdf_count-1 = pdf, pdf_count = Back */

    for (;;) {
        int max_main = 1;
        if (screen == SCREEN_MAIN) {
            if (main_sel < 0)
                main_sel = 0;
            if (main_sel > max_main)
                main_sel = max_main;
            draw_main_menu(main_sel);
        } else {
            int max_lib = (int)pdf_count; /* last index is Back */
            if (library_sel < 0)
                library_sel = 0;
            if (library_sel > max_lib)
                library_sel = max_lib;
            draw_library(library_sel);
        }

        int ch = getch();

        if (ch == 'q' || ch == 'Q') {
            if (screen == SCREEN_LIBRARY) {
                screen = SCREEN_MAIN;
                continue;
            }
            break;
        }

        if (screen == SCREEN_MAIN) {
            switch (ch) {
            case KEY_UP:
                main_sel--;
                if (main_sel < 0)
                    main_sel = max_main;
                break;
            case KEY_DOWN:
                main_sel++;
                if (main_sel > max_main)
                    main_sel = 0;
                break;
            case KEY_ENTER:
            case '\n':
            case '\r':
                if (main_sel == 0) {
                    scan_library();
                    library_sel = 0;
                    screen = SCREEN_LIBRARY;
                } else {
                    mvprintw(LINES - 1, 2, "Power off? (y/N)");
                    refresh();
                    int c2 = getch();
                    if (c2 == 'y' || c2 == 'Y')
                        try_poweroff();
                }
                break;
            default:
                break;
            }
        } else {
            int max_lib = (int)pdf_count;
            switch (ch) {
            case KEY_UP:
                library_sel--;
                if (library_sel < 0)
                    library_sel = max_lib;
                break;
            case KEY_DOWN:
                library_sel++;
                if (library_sel > max_lib)
                    library_sel = 0;
                break;
            case KEY_ENTER:
            case '\n':
            case '\r':
                if (library_sel == max_lib || pdf_count == 0) {
                    screen = SCREEN_MAIN;
                } else {
                    open_pdf(pdf_paths[library_sel]);
                }
                break;
            case 27: /* ESC */
                screen = SCREEN_MAIN;
                break;
            default:
                break;
            }
        }
    }

    endwin();
    return 0;
}
