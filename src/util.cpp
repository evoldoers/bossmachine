// feature test macro requirement for ftw
// #define _XOPEN_SOURCE 500

// includes
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <ftw.h>

#include "util.h"
#include "stacktrace.h"
#include "logger.h"

// buffer size for popen
#define PIPE_BUF_SIZE 1024

// buffer size for getcwd
#define DIR_BUF_SIZE 4096

using namespace MachineBoss;

// function defs
void MachineBoss::Warn(const char* warning, ...) {
  va_list argptr;
  fprintf(stderr,"Warning: ");
  va_start (argptr, warning);
  vfprintf(stderr,warning,argptr);
  fprintf(stderr,"\n");
  va_end (argptr);
}

void MachineBoss::Abort(const char* error, ...) {
  va_list argptr;
  va_start (argptr, error);
  fprintf(stderr,"Abort: ");
  vfprintf(stderr,error,argptr);
  fprintf(stderr,"\n");
  va_end (argptr);
  printStackTrace();
  throw runtime_error("Abort");
}

void MachineBoss::Fail(const char* error, ...) {
  va_list argptr;
  va_start (argptr, error);
  vfprintf(stderr,error,argptr);
  fprintf(stderr,"\n");
  va_end (argptr);
  exit (EXIT_FAILURE);
}

std::string MachineBoss::plural (long n, const char* singular) {
  std::string s = std::to_string(n) + " " + singular;
  if (n != 1)
    s += "s";
  return s;
}

std::string MachineBoss::plural (long n, const char* singular, const char* plural) {
  std::string s = std::to_string(n) + " " + (n == 1 ? singular : plural);
  return s;
}

std::vector<std::string> MachineBoss::split (const std::string& s, const char* splitChars) {
  std::vector<std::string> result;
  auto b = s.begin();
  while (true) {
    while (b != s.end() && strchr (splitChars, *b) != NULL)
      ++b;
    if (b == s.end())
      break;
    auto e = b;
    while (e != s.end() && strchr (splitChars, *e) == NULL)
      ++e;
    result.push_back (string (b, e));
    b = e;
  }
  return result;
}

std::vector<std::string> MachineBoss::splitToChars (const std::string& s) {
  std::vector<std::string> result;
  result.reserve (s.size());
  for (const char c: s)
    result.push_back (string (1, c));
  return result;
}

std::string MachineBoss::toupper (const std::string& s) {
  std::string r (s);
  for (size_t n = 0; n < r.size(); ++n)
    r[n] = std::toupper (r[n]);
  return r;
}

char const* const hexdig = "0123456789ABCDEF";
void MachineBoss::write_escaped (std::string const& s, std::ostream& out) {
  for (std::string::const_iterator i = s.begin(), end = s.end(); i != end; ++i) {
    unsigned char c = *i;
    if (' ' <= c and c <= '~' and c != '\\' and c != '"') {
      out << c;
    }
    else {
      out << '\\';
      switch(c) {
      case '"':  out << '"';  break;
      case '\\': out << '\\'; break;
      case '\t': out << 't';  break;
      case '\r': out << 'r';  break;
      case '\n': out << 'n';  break;
      default:
        out << 'x';
        out << hexdig[c >> 4];
        out << hexdig[c & 0xF];
      }
    }
  }
}

std::string MachineBoss::escaped_str (std::string const& s) {
  std::ostringstream outs;
  write_escaped (s, outs);
  return outs.str();
}
