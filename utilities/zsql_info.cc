
#include <stdio.h>
#include <stdlib.h>

#include "my_getopt.h"
#include "my_sys.h"
#include "mysql_version.h"
#include "git_version.h"

static struct my_option my_long_options[] =
{
  {"help", '?', "Displays this help and exits.",
   0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"version", 'v',
   "Displays version information in one line and exits.",
   0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"verbose-version", 'V',
   "Displays verbose version information in multi-lines and exits.",
   0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};

static void usage(void)
{
  printf("Print a description for ZSQL server version.\n");
  printf("Usage: %s [OPTIONS]\n", my_progname);
  my_print_help(my_long_options);
}

static void print_version(void)
{
  printf("%s rev:%s\n", MYSQL_GOLDENDB_VERSION, GIT_VERSION);
}

static void print_verbose_version(void)
{
  printf("version:%s\n", MYSQL_GOLDENDB_VERSION);
  printf("git_rev:%s\n", GIT_VERSION);
}

static bool get_one_option(int optid,
                           const struct my_option *opt MY_ATTRIBUTE((unused)),
                           char *argument MY_ATTRIBUTE((unused)))
{
  switch (optid) {
    case 'v':
      print_version();
      exit(0);
    case 'V':
      print_verbose_version();
      exit(0);
    case '?':
      usage();
      exit(0);
  }
  return 0;
}

static int get_options(int *argc, char ***argv)
{
  int ho_error;

  if ((ho_error = handle_options(argc, argv, my_long_options, get_one_option)))
    exit(ho_error);

  if (!*argc) {
    usage();
    return 1;
  }
  return 0;
} /* get_options */


int main(int argc, char *argv[])
{
  MY_INIT(argv[0]);

  if (get_options(&argc, &argv)) {
    exit(1);
  }

  return 0;
}
