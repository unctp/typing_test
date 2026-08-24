#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
/*
 * "You have no heart. . ."
 *
 * /\__/\
 * \    /
 *  \  /
 *   \/
 */

#define ESC "\033"
#define CSI ESC "["

#define RESET CSI "0m"
#define BOLD CSI "1m"
#define DIM CSI "2m"

#define RED CSI "31m"
#define GREEN CSI "32m"
#define YELLOW CSI "33m"
#define BLUE CSI "34m"
#define CYAN CSI "36m"
#define WHITE CSI "37m"

#define CLEAR CSI "2J"
#define HOME CSI "H"
#define HIDE_CURSOR CSI "?25l"
#define SHOW_CURSOR CSI "?25h"

static struct termios original_termios;

static const char *text =
    "The quick brown fox jumps over the lazy dog. "
    "Good typing is not about smashing keys as quickly as possible. "
    "Accuracy comes first, because fixing mistakes costs more time "
    "than typing carefully in the first place. "
    "Sometimes the best programs are the kind that do exactly what they need "
    "to and nothing more.";

typedef struct {
  int cols;
  int rows;
} TerminalSize;

typedef struct {
  size_t start;
  size_t length;
} Line;

// terminal handling

static void disable_raw_mode(void) {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
  printf(SHOW_CURSOR RESET CLEAR HOME);
  fflush(stdout);
}

static void enable_raw_mode(void) {
  struct termios raw;

  tcgetattr(STDIN_FILENO, &original_termios);
  atexit(disable_raw_mode);

  raw = original_termios;

  raw.c_lflag &= ~(ECHO | ICANON | ISIG);
  raw.c_iflag &= ~(IXON | ICRNL);
  raw.c_oflag &= ~(OPOST);

  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static TerminalSize get_terminal_size(void) {
  struct winsize ws;
  TerminalSize size = {80, 24};

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    if (ws.ws_col > 0)
      size.cols = ws.ws_col;

    if (ws.ws_row > 0)
      size.rows = ws.ws_row;
  }

  return size;
}

static void move_to(int row, int col) { printf(CSI "%d;%dH", row, col); }

// text wrapping

/*
 * wraps text by words.

 * each Line points into the original text, so no extra strings
 * need to be allocated for every line
 */
static size_t wrap_text(const char *source, int width, Line **out_lines) {
  size_t length = strlen(source);
  size_t capacity = 16;
  size_t count = 0;

  Line *lines = malloc(capacity * sizeof(*lines));

  if (!lines)
    return 0;

  size_t position = 0;

  while (position < length) {
    while (position < length && isspace((unsigned char)source[position])) {
      position++;
    }

    if (position >= length)
      break;

    size_t line_start = position;
    size_t last_space = position;
    size_t line_end = position;

    while (position < length) {
      if (source[position] == '\n')
        break;

      if (isspace((unsigned char)source[position]))
        last_space = position;

      if ((int)(position - line_start + 1) > width) {
        if (last_space > line_start) {
          line_end = last_space;
          position = last_space + 1;
        } else {
          /* single word longer than available width. */
          line_end = position;
        }

        break;
      }

      position++;
      line_end = position;
    }

    if (position >= length || source[position] == '\n') {
      line_end = position;

      if (source[position] == '\n')
        position++;
    }

    while (line_end > line_start &&
           isspace((unsigned char)source[line_end - 1])) {
      line_end--;
    }

    if (count == capacity) {
      capacity *= 2;

      Line *new_lines = realloc(lines, capacity * sizeof(*lines));

      if (!new_lines) {
        free(lines);
        return 0;
      }

      lines = new_lines;
    }

    lines[count].start = line_start;
    lines[count].length = line_end - line_start;
    count++;
  }

  *out_lines = lines;
  return count;
}

// rendering

static void draw_border(int row, int col, int width, int height) {
  move_to(row, col);
  printf("+");

  for (int i = 0; i < width - 2; i++)
    printf("-");

  printf("+");

  for (int y = 1; y < height - 1; y++) {
    move_to(row + y, col);
    printf("|");

    move_to(row + y, col + width - 1);
    printf("|");
  }

  move_to(row + height - 1, col);
  printf("+");

  for (int i = 0; i < width - 2; i++)
    printf("-");

  printf("+");
}

static void render_text(const char *source, const char *input, size_t typed, int top_row,
                        int left_col, int width) {
  Line *lines = NULL;
  size_t line_count = wrap_text(source, width, &lines);

  for (size_t i = 0; i < line_count; i++) {
    move_to(top_row + (int)i, left_col);

    for (size_t j = 0; j < lines[i].length; j++) {
      size_t index = lines[i].start + j;
      char expected = source[index];

      if (index < typed) {
        char actual = input[index];

        /*
         * typed characters corespond to the source index
         *
         * correct: green
         * wrong:   red background-ish emphasis
         */
        if (actual == expected)
          printf(GREEN "%c" RESET, expected);
        else
          printf(RED "%c" RESET, expected);
      } else if (index == typed) {
        printf(CYAN BOLD "%c" RESET, expected);
      } else {
        printf(DIM "%c" RESET, expected);
      }
    }
  }

  free(lines);
}

static void render_test(const char *source, const char *input,
		                        size_t typed, int errors,
					double elapsed, int started) {
  TerminalSize term = get_terminal_size();

  int box_width = term.cols - 4;

  if (box_width < 30)
    box_width = 30;

  int text_width = box_width - 4;

  printf(CLEAR HOME HIDE_CURSOR);

  move_to(1, 3);
  printf(BOLD CYAN "TUI TYPING TEST" RESET);

  move_to(2, 3);

  if (!started) {
    printf(DIM "Start typing to begin. ESC quits." RESET);
  } else {
    double minutes = elapsed / 60.0;
    double wpm = minutes > 0.0 ? (typed / 5.0) / minutes : 0.0;

    printf("Time: %.1fs | WPM: %.1f | Errors: %d", elapsed, wpm, errors);
  }

  Line *lines = NULL;
  size_t line_count = wrap_text(source, text_width, &lines);

  int box_height = (int)line_count + 2;

  draw_border(4, 3, box_width, box_height);

  render_text(source, input, typed, 5, 5, text_width);

  move_to(5 + (int)line_count + 2, 3);
  printf(DIM "Backspace = correct mistakes | ESC = quit" RESET);

  fflush(stdout);

  free(lines);
}

// main

int main(void) {
  size_t text_length = strlen(text);
  char *input = calloc(text_length + 1, sizeof(*input));

  if (!input) {
    perror("calloc");
    return EXIT_FAILURE;
  }
  size_t typed = 0;

  int errors = 0;
  int started = 0;

  struct timespec start_time = {0};

  enable_raw_mode();

  while (typed < text_length) {
    double elapsed = 0.0;

    if (started) {
      struct timespec now;

      clock_gettime(CLOCK_MONOTONIC, &now);

      elapsed = (now.tv_sec - start_time.tv_sec) +
                (now.tv_nsec - start_time.tv_nsec) / 1e9;
    }

    render_test(text, input, typed, errors, elapsed, started);

    unsigned char c;

    if (read(STDIN_FILENO, &c, 1) != 1)
      break;

    if (c == 27) { /* ESC */
      break;
    }

    if (c == 127 || c == 8) {
      if (typed > 0)
        typed--;

      continue;
    }

    if (!started && isprint(c)) {
      started = 1;
      clock_gettime(CLOCK_MONOTONIC, &start_time);
    }

    if (isprint(c) && typed < text_length) {
      if (c != (unsigned char)text[typed])
        errors++;

      typed++;
    }
  }

  if (typed == text_length) {
    struct timespec end_time;

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double elapsed = (end_time.tv_sec - start_time.tv_sec) +
                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    double minutes = elapsed / 60.0;
    double wpm = minutes > 0.0 ? (typed / 5.0) / minutes : 0.0;

    double accuracy =
        typed > 0 ? ((double)(typed - errors) / typed) * 100.0 : 100.0;

    printf(CLEAR HOME);

    printf(BOLD GREEN "TEST COMPLETE\n\n" RESET);

    printf("Time:     %.2f seconds\n", elapsed);
    printf("Speed:    %.2f WPM\n", wpm);
    printf("Errors:   %d\n", errors);
    printf("Accuracy: %.2f%%\n\n", accuracy);

    printf(DIM "Press Enter to exit." RESET);
    fflush(stdout);

    unsigned char c;

    while (read(STDIN_FILENO, &c, 1) == 1) {
      if (c == '\r' || c == '\n')
        break;
    }
  }

  return 0;
}
