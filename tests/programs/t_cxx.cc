// C++: exceptions, iostreams, the standard containers, static constructors.
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Shape { virtual ~Shape () {} virtual double area () const = 0; };
struct Square : Shape { double s; Square (double s) : s (s) {} double area () const override { return s * s; } };

static std::string made_early = std::string ("static ") + "constructor";

static int depth (int n) { if (n == 0) throw std::runtime_error ("bottom"); return depth (n - 1) + 1; }

int main ()
{
  int fails = 0;
  try { depth (50); fails++; }
  catch (const std::exception &e) { std::cout << "caught: " << e.what () << '\n'; }
  std::map<std::string, int> m;
  for (const char *w : { "riscos", "linux", "riscos", "test" }) m[w]++;
  std::ostringstream os;
  for (auto &p : m) os << p.first << '=' << p.second << ' ';
  std::cout << os.str () << '\n';
  if (os.str () != "linux=1 riscos=2 test=1 ") fails++;
  std::vector<std::unique_ptr<Shape>> v;
  for (int i = 1; i <= 3; i++) v.emplace_back (new Square (i));
  double a = 0;
  for (auto &s : v) a += s->area ();
  std::cout << "area " << a << '\n';
  if (a != 14.0) fails++;
  std::cout << made_early << '\n';
  if (made_early != "static constructor") fails++;
  std::cout << (fails ? "FAIL" : "PASS") << std::endl;
  return fails;
}
