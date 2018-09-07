/*
 * *****************************************************************************
 *
 * Copyright 2018 Gavin D. Howard
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * *****************************************************************************
 *
 * Generates a const array from a bc script.
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>

#include <libgen.h>

static const char* const bc_gen_usage = "usage: gen input output name [label]\n";

static const char* const bc_gen_header =
  "// *** AUTOMATICALLY GENERATED FROM %s. DO NOT MODIFY. ***\n";

static const char* const bc_gen_label = "const char *%s = \"%s\";\n\n";
static const char* const bc_gen_name = "const char %s[] = {\n";

#define INVALID_PARAMS (1)
#define MALLOC_FAIL (2)
#define INVALID_INPUT_FILE (3)
#define INVALID_OUTPUT_FILE (4)
#define INVALID_HEADER_FILE (5)
#define IO_ERR (6)

#define MAX_WIDTH (74)

int main(int argc, char *argv[]) {

  FILE *in, *out;
  char *header_buf, *buf, *base, *label, *name;
  int c, count, err, slashes;
  bool has_label;

  err = 0;

  if (argc < 4) {
    printf(bc_gen_usage);
    return INVALID_PARAMS;
  }

  name = argv[3];

  has_label = argc > 4;
  label = has_label ? argv[4] : "";

  buf = malloc(strlen(argv[1]) + 1);

  if (!buf) return MALLOC_FAIL;

  strcpy(buf, argv[1]);

  base = basename(buf);

  in = fopen(argv[1], "r");

  if (!in) {
    err = INVALID_INPUT_FILE;
    goto in_err;
  }

  out = fopen(argv[2], "w");

  if (!out) {
    err = INVALID_OUTPUT_FILE;
    goto out_err;
  }

  header_buf = malloc(strlen(bc_gen_header) + strlen(base) + 1);

  if (!header_buf) {
    err= MALLOC_FAIL;
    goto header_buf_err;
  }

  if (sprintf(header_buf, bc_gen_header, base) < 0) {
    err = IO_ERR;
    goto error;
  }

  if (fputs(header_buf, out) == EOF) {
    err = IO_ERR;
    goto error;
  }

  if (has_label && fprintf(out, bc_gen_label, label, base) < 0) {
    err = IO_ERR;
    goto error;
  }

  if (fprintf(out, bc_gen_name, name) < 0) {
    err = IO_ERR;
    goto error;
  }

  c = count = slashes = 0;

  while (slashes < 2 && (c = fgetc(in)) >= 0) {
    if (slashes == 1 && c == '/' && fgetc(in) == '\n') ++slashes;
    if (!slashes && c == '/' && fgetc(in) == '*') ++slashes;
  }

  if (c < 0) {
    err = INVALID_INPUT_FILE;
    goto error;
  }

  while ((c = fgetc(in)) >= 0) {

    int val;

    if (!count) {
      if (fprintf(out, "  ") < 0) {
        err = IO_ERR;
        goto error;
      }
    }

    val = fprintf(out, "%d,", c);

    if (val < 0) {
      err = IO_ERR;
      goto error;
    }

    count += val;

    if (count > MAX_WIDTH) {

      count = 0;

      if (fputc('\n', out) == EOF) {
        err = IO_ERR;
        goto error;
      }
    }
  }

  if (!count) {
    if (fputc(' ', out) == EOF || fputc(' ', out) == EOF) {
      err = IO_ERR;
      goto error;
    }
  }
  if (fprintf(out, "0\n};\n") < 0) {
    err = IO_ERR;
    goto error;
  }

error:

  free(header_buf);

header_buf_err:

  fclose(out);

out_err:

  fclose(in);

in_err:

  free(buf);

  return err;
}
