// Consistency of the interface strings (i18n.cpp).
#include "framework.h"
#include "shim/Arduino.h"
#include "../i18n.h"
#include "../pet.h"  // MED_COUNT
#include <ctype.h>
#include <string.h>
#include <vector>

// extracts the printf specifier signature of a string: "%lud" -> "lu"
static std::vector<std::string> specs(const char *s) {
  std::vector<std::string> out;
  const char *p = s;
  while (*p) {
    if (*p != '%') { p++; continue; }
    p++;
    if (*p == '%') { p++; continue; }   // %% is a literal
    std::string sig;
    while (*p && strchr("-+ #0", *p)) p++;             // flags
    while (*p && isdigit((unsigned char)*p)) p++;      // width
    if (*p == '.') { p++; while (*p && isdigit((unsigned char)*p)) p++; }
    while (*p && strchr("lhzjt", *p)) { sig += *p; p++; }  // modifier
    if (!*p) { out.push_back(sig + "<incompleto>"); break; }
    sig += *p++;
    out.push_back(sig);
  }
  return out;
}

static std::string joinSpecs(const std::vector<std::string> &v) {
  std::string s;
  for (size_t i = 0; i < v.size(); i++) { if (i) s += ","; s += "%" + v[i]; }
  return s.empty() ? "(sin formato)" : s;
}

// expected signature of each format string, taken from the sketch calls
struct ExpectedFmt { StrId id; const char *sig; };
static const ExpectedFmt EXPECTED[] = {
  { S_POKEDEX_FMT,     "%u" },
  { S_NAME_FMT,        "%s,%s,%u" },
  { S_RELEASE_FMT,     "%s" },
  { S_HITS_FMT,        "%u" },
  { S_STR_GAIN_FMT,    "%u" },
  { S_RECORD_FMT,      "%u" },
  { S_SCORE_FMT,       "%u" },
  { S_STREAK_DAYS_FMT, "%u" },
  { S_STREAK_FMT,      "%u,%u" },
  { S_INFO_FMT,        "%s,%lu" },
  { S_MEDALS_FMT,      "%d,%d" },
  { S_REC_FMT,         "%u" },
  { S_LVL_FMT,         "%u" },
  { S_NEXT_LVL_FMT,    "%u,%u" },
  { S_EVO_IN_FMT,      "%u" },
  { S_MISTAKES_FMT,    "%u" },
  { S_FAREWELL_BTN,    "%s" },
  { S_RUNAWAY_BTN,     "%s" },
};
static const int NUM_EXPECTED = (int)(sizeof(EXPECTED) / sizeof(EXPECTED[0]));

static const char *expectedSig(int id) {
  for (int i = 0; i < NUM_EXPECTED; i++)
    if ((int)EXPECTED[i].id == id) return EXPECTED[i].sig;
  return "(sin formato)";
}

TEST(i18n, ninguna_cadena_tiene_huecos) {
  for (int id = 0; id < STR_COUNT; id++) {
    const char *s = T((StrId)id);
    char where[64];
    snprintf(where, sizeof(where), "[id %d]", id);
    CHECK_MSG(s != NULL, where);
    if (s) CHECK_MSG(s[0] != 0, std::string("cadena vacia ") + where);
  }
}

// The firmware font is the 5x7 one from Arduino_GFX and the UI centers with strlen(t)*6: everything
// must be ASCII, a multi-byte UTF-8 character would throw the label off.
TEST(i18n, las_cadenas_son_ascii) {
  for (int id = 0; id < STR_COUNT; id++)
    for (const char *c = T((StrId)id); *c; c++)
      CHECK_MSG((unsigned char)*c < 128, std::string("no ASCII en: ") + T((StrId)id));
}

// the formats must match what the sketch passes in each call: an extra or
// missing %u is a read of garbage from the stack
TEST(i18n, los_formatos_cuadran_con_las_llamadas_del_sketch) {
  for (int id = 0; id < STR_COUNT; id++) {
    std::string got = joinSpecs(specs(T((StrId)id)));
    const char *want = expectedSig(id);
    char msg[256];
    snprintf(msg, sizeof(msg), "[id %d] \"%s\": %s, se esperaba %s", id, T((StrId)id), got.c_str(), want);
    CHECK_MSG(got == want, msg);
  }
}

TEST(i18n, las_medallas_tienen_todos_los_textos) {
  for (int m = 0; m < MED_COUNT; m++) {
    const char *n = medalName(m), *d = medalDesc(m);
    char where[64];
    snprintf(where, sizeof(where), "[medalla %d]", m);
    CHECK_MSG(n && n[0], std::string("medalName vacio ") + where);
    CHECK_MSG(d && d[0], std::string("medalDesc vacio ") + where);
    if (n) CHECK_MSG(strlen(n) <= 12, std::string("nombre demasiado largo ") + where + ": " + n);
    if (d) CHECK_MSG(strlen(d) <= 16, std::string("descripcion demasiado larga ") + where + ": " + d);
  }
}
