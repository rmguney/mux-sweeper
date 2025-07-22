#ifndef ARGUMENTS_H
#define ARGUMENTS_H

#include "params.h"

// Command line argument parsing functions
void arguments_print_usage(const char* program_name);
int arguments_parse(int argc, char* argv[], capture_params_t* params);

#endif // ARGUMENTS_H
