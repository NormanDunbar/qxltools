#ifndef UNIX	/* avoid conflict with stdlib.h */
int getopt(int argc, char **argv, char *opts);
extern int optind;
extern char *optarg;
#endif
