#include "writer_core.h"
#include <cstring>
namespace writer {
bool valid_utf8(const char *s, std::size_t n) {
  for (std::size_t i = 0; i < n;) {
    unsigned c = static_cast<unsigned char>(s[i++]);
    if (!c)
      return false;
    if (c < 128)
      continue;
    unsigned more = 0, value = 0, minimum = 0;
    if (c >= 0xc2 && c <= 0xdf) {
      more = 1;
      value = c & 31;
      minimum = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      more = 2;
      value = c & 15;
      minimum = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      more = 3;
      value = c & 7;
      minimum = 0x10000;
    } else
      return false;
    if (i + more > n)
      return false;
    while (more--) {
      unsigned d = static_cast<unsigned char>(s[i++]);
      if ((d & 0xc0) != 0x80)
        return false;
      value = (value << 6) | (d & 63);
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff))
      return false;
  }
  return true;
}
namespace {
bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
int days(int m, int y) {
  static const int d[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && leap(y) ? 29 : (m >= 1 && m <= 12 ? d[m] : 0);
}
bool same(const char *a, const char *b) {
  while (*a && *b) {
    char x = *a++, y = *b++;
    if (x >= 'A' && x <= 'Z')
      x += 32;
    if (y >= 'A' && y <= 'Z')
      y += 32;
    if (x != y)
      return false;
  }
  return !*a && !*b;
}
int compare(Date a, Date b) {
  if (a.year != b.year)
    return a.year - b.year;
  if (a.month != b.month)
    return a.month - b.month;
  return a.day - b.day;
}
const char *normal(char d, bool layer, int n) {
  static const char *l0[4] = {"abc", "hij", "nop", "tuw"};
  static const char *l1[4] = {"def", "klm", "qrs", "xyz"};
  int i = d == 'U' ? 0 : d == 'R' ? 1 : d == 'D' ? 2 : d == 'L' ? 3 : -1;
  return i < 0 ? nullptr : (layer ? l1[i] : l0[i]) + n;
}
} // namespace
bool valid_date(Date d) {
  return d.year >= 1 && d.year <= 9999 && d.month >= 1 && d.month <= 12 &&
         d.day >= 1 && d.day <= days(d.month, d.year);
}
bool parse_diary_name(const char *n, Date &d) {
  if (!n || std::strlen(n) != 12 || !same(n + 8, ".txt"))
    return false;
  for (int i = 0; i < 8; ++i)
    if (n[i] < '0' || n[i] > '9')
      return false;
  d = {(n[0] - '0') * 10 + n[1] - '0', (n[2] - '0') * 10 + n[3] - '0',
       (n[4] - '0') * 1000 + (n[5] - '0') * 100 + (n[6] - '0') * 10 + n[7] -
           '0'};
  return valid_date(d);
}
void format_diary_name(Date d, char o[13]) {
  o[0] = '0' + d.day / 10;
  o[1] = '0' + d.day % 10;
  o[2] = '0' + d.month / 10;
  o[3] = '0' + d.month % 10;
  o[4] = '0' + d.year / 1000;
  o[5] = '0' + d.year / 100 % 10;
  o[6] = '0' + d.year / 10 % 10;
  o[7] = '0' + d.year % 10;
  o[8] = '.';
  o[9] = 't';
  o[10] = 'x';
  o[11] = 't';
  o[12] = 0;
}
Date next_day(Date d) {
  if (++d.day > days(d.month, d.year)) {
    d.day = 1;
    if (++d.month > 12) {
      d.month = 1;
      ++d.year;
    }
  }
  return d;
}
bool latest_diary_date(const char *const *n, int c, Date &out) {
  bool found = false;
  for (int i = 0; i < c; ++i) {
    Date d{};
    if (parse_diary_name(n[i], d) && (!found || compare(d, out) > 0)) {
      out = d;
      found = true;
    }
  }
  return found;
}
bool can_create_new(const char *n, const char *const *names, int count) {
  for (int i = 0; i < count; ++i)
    if (same(n, names[i]))
      return false;
  return true;
}
TextModel::TextModel()
    : _gap_begin(0), _gap_end(TEXT_CAPACITY), _size(0), _dirty(false) {
  _storage[0] = 0;
}
std::size_t TextModel::bytes() const { return _size; }
std::size_t TextModel::caret_byte() const { return _gap_begin; }
bool TextModel::dirty() const { return _dirty; }
void TextModel::mark_saved() { _dirty = false; }
const char *TextModel::data() {
  _storage[_size] = 0;
  return _storage;
}
void TextModel::move_gap(std::size_t p) {
  if (p > _size)
    p = _size;
  _gap_begin = p;
}
std::size_t TextModel::previous_utf8(const char *s, std::size_t p) {
  if (!p)
    return 0;
  do {
    --p;
  } while (p && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80);
  return p;
}
std::size_t TextModel::next_utf8(const char *s, std::size_t n, std::size_t p) {
  if (p >= n)
    return n;
  int k = (static_cast<unsigned char>(s[p]) < 0x80)           ? 1
          : (static_cast<unsigned char>(s[p]) & 0xE0) == 0xC0 ? 2
          : (static_cast<unsigned char>(s[p]) & 0xF0) == 0xE0 ? 3
          : (static_cast<unsigned char>(s[p]) & 0xF8) == 0xF0 ? 4
                                                              : 1;
  return p + k <= n ? p + k : p + 1;
}
bool TextModel::set_text(const char *s) {
  std::size_t n = std::strlen(s);
  if (n > TEXT_CAPACITY || !valid_utf8(s, n))
    return false;
  std::memcpy(_storage, s, n);
  _size = n;
  _gap_begin = n;
  _gap_end = TEXT_CAPACITY;
  _dirty = false;
  return true;
}
bool TextModel::insert(const char *s) {
  std::size_t n = std::strlen(s);
  if (n > TEXT_CAPACITY - _size || !valid_utf8(s, n))
    return false;
  std::memmove(_storage + _gap_begin + n, _storage + _gap_begin,
               _size - _gap_begin);
  std::memcpy(_storage + _gap_begin, s, n);
  _gap_begin += n;
  _size += n;
  _dirty = true;
  return true;
}
bool TextModel::replace_before_caret(const char *s) {
  std::size_t n = std::strlen(s),
              removed = _gap_begin - previous_utf8(data(), _gap_begin);
  if (n > TEXT_CAPACITY - _size + removed || !valid_utf8(s, n))
    return false;
  if (removed)
    backspace();
  return insert(s);
}
bool TextModel::backspace() {
  if (!_gap_begin)
    return false;
  std::size_t p = previous_utf8(data(), _gap_begin);
  std::memmove(_storage + p, _storage + _gap_begin, _size - _gap_begin);
  _size -= (_gap_begin - p);
  _gap_begin = p;
  _dirty = true;
  return true;
}
void TextModel::set_caret(std::size_t p) {
  if(p>_size)p=_size;
  while(p && p<_size && (static_cast<unsigned char>(_storage[p])&0xc0)==0x80)--p;
  move_gap(p);
}
void TextModel::move_left() { move_gap(previous_utf8(data(), _gap_begin)); }
void TextModel::move_right() { move_gap(next_utf8(data(), _size, _gap_begin)); }
void TextModel::move_home() { move_gap(0); }
void TextModel::move_end() { move_gap(_size); }
const char *alternate_letter(char b, int i) {
  static const char *const A[] = {"á", "ä", "à", "â", "ã", "å", "æ"};
  static const char *const C[] = {"ç", "č", "ć"};
  static const char *const E[] = {"é", "è", "ë", "ê"};
  static const char *const I[] = {"í", "ï", "ì", "î"};
  static const char *const N[] = {"ñ", "ń"};
  static const char *const O[] = {"ó", "ö", "ô", "ò", "õ", "ø", "œ"};
  static const char *const S[] = {"ß", "š", "ś"};
  static const char *const U[] = {"ü", "ú", "ù", "û"};
  static const char *const Y[] = {"ý", "ÿ"};
  static const char *const Z[] = {"ž", "ź", "ż"};
  const char *const *a = nullptr;
  int n = 0;
  switch (b) {
  case 'a':
    a = A;
    n = 7;
    break;
  case 'c':
    a = C;
    n = 3;
    break;
  case 'e':
    a = E;
    n = 4;
    break;
  case 'i':
    a = I;
    n = 4;
    break;
  case 'n':
    a = N;
    n = 2;
    break;
  case 'o':
    a = O;
    n = 7;
    break;
  case 's':
    a = S;
    n = 3;
    break;
  case 'u':
    a = U;
    n = 4;
    break;
  case 'y':
    a = Y;
    n = 2;
    break;
  case 'z':
    a = Z;
    n = 3;
    break;
  default:
    return nullptr;
  }
  return a[i % n];
}
InputState::InputState()
    : _held(0), _shift(false), _caps(false), _start_used(false), _select(false),
      _group_r_count(0), _select_value{"."}, _alternate_base(0) {}
const char* InputState::active_group() const {
  unsigned directions=_held&15;
  if(held(Button::START) || !directions || (directions&(directions-1)))return "";
  char dir=held(Button::UP)?'U':held(Button::RIGHT)?'R':held(Button::DOWN)?'D':'L';
  const char* lower=normal(dir,held(Button::L),0);
  if(!(_shift||_caps))return lower;
  static const char* upper[]={"ABC","HIJ","NOP","TUW","DEF","KLM","QRS","XYZ"};
  int index=dir=='U'?0:dir=='R'?1:dir=='D'?2:3;
  return upper[index+(held(Button::L)?4:0)];
}
bool InputState::held(Button b) const { return _held & (1u << unsigned(b)); }
void InputState::set(Button b, bool on) {
  if (on)
    _held |= 1u << unsigned(b);
  else
    _held &= ~(1u << unsigned(b));
}
const char *InputState::case_text(const char *lower, char) {
  std::strcpy(_output, lower);
  if (!(_select_existing ? _letter_upper : (_caps || _shift)))
    return _output;
  if (static_cast<unsigned char>(lower[0]) < 128) {
    if (lower[0] >= 'a' && lower[0] <= 'z')
      _output[0] -= 32;
    return _output;
  }
  static const char *lows[] = {"á", "ä", "à", "â", "ã", "å", "æ", "ç", "č", "ć",
                               "é", "è", "ë", "ê", "í", "ï", "ì", "î", "ñ", "ń",
                               "ó", "ö", "ô", "ò", "õ", "ø", "œ", "š", "ś", "ü",
                               "ú", "ù", "û", "ý", "ÿ", "ž", "ź", "ż"};
  static const char *ups[] = {"Á", "Ä", "À", "Â", "Ã", "Å", "Æ", "Ç", "Č", "Ć",
                              "É", "È", "Ë", "Ê", "Í", "Ï", "Ì", "Î", "Ñ", "Ń",
                              "Ó", "Ö", "Ô", "Ò", "Õ", "Ø", "Œ", "Š", "Ś", "Ü",
                              "Ú", "Ù", "Û", "Ý", "Ÿ", "Ž", "Ź", "Ż"};
  for (unsigned i = 0; i < sizeof(lows) / sizeof(*lows); ++i)
    if (!std::strcmp(lower, lows[i])) {
      std::strcpy(_output, ups[i]);
      break;
    }
  return _output;
}
InputEvent InputState::letter_event(char base, bool select) {
  if (select) {
    if (!alternate_letter(base, 0))
      return {EventKind::NONE, ""};
    _alternate_index = _alternate_base == base ? _alternate_index + 1 : 0;
    _alternate_base = base;
    std::strcpy(_select_value,
                case_text(alternate_letter(base, _alternate_index), base));
    _select_alpha = true;
    return {EventKind::REPLACE, _select_value};
  }
  _alternate_base = 0;
  char lower[2] = {base, 0};
  const char *x = case_text(lower, base);
  _shift = false;
  return {EventKind::INSERT, x};
}
InputEvent InputState::press(Button b, bool fresh) {
  set(b, true);
  // Public events own these edges too; update must not invent a second press.
  _previous |= 1u << unsigned(b);
  if (_held != (1u << unsigned(Button::R))) _r_pending = false;
  if ((_held & ~(1u << unsigned(Button::SELECT))) != _letter_keys)
    _letter_keys = 0;
  if (b == Button::L || b == Button::A || b == Button::B ||
      b == Button::START || b == Button::SELECT)
    _group_r_count = 0;
  if (b == Button::L)
    _alternate_base = 0;
  if (_select && b == Button::START) {
    _start_used = true;
    return {EventKind::NONE, ""};
  }
  if (!fresh)
    return {EventKind::NONE, ""};
  if (!_select && held(Button::START) && b != Button::START) {
    _start_used = true;
    switch (b) {
    case Button::LEFT:
      return {EventKind::MOVE_LEFT, ""};
    case Button::RIGHT:
      return {EventKind::MOVE_RIGHT, ""};
    case Button::UP:
      return {EventKind::MOVE_UP, ""};
    case Button::DOWN:
      return {EventKind::MOVE_DOWN, ""};
    case Button::L:
      return {EventKind::PAGE_PREV, ""};
    case Button::R:
      return {EventKind::PAGE_NEXT, ""};
    case Button::A:
      return {EventKind::SAVE, ""};
    case Button::B:
      return {EventKind::SAVE_MENU, ""};
    default:
      return {EventKind::NONE, ""};
    }
  }
  if (b == Button::SELECT) {
    _select = true;
    _alternate_base = 0;
    if (_letter_keys && (_held & ~(1u << unsigned(Button::SELECT))) == _letter_keys) {
      _select_existing = true;
      return letter_event(_letter_base, true);
    }
    _select_existing = false;
    std::strcpy(_select_value, ".");
    return {EventKind::INSERT, "."};
  }
  char dir = held(Button::UP)      ? 'U'
             : held(Button::RIGHT) ? 'R'
             : held(Button::DOWN)  ? 'D'
             : held(Button::LEFT)  ? 'L'
                                   : 0;
  unsigned directions=_held&15;
  if(directions&&(directions&(directions-1)))return {EventKind::NONE,""};
  bool layer = held(Button::L);
  if (dir && (b == Button::A || b == Button::B || b == Button::R)) {
    int n = b == Button::B ? 0 : b == Button::A ? 1 : 2;
    const char *p = normal(dir, layer, n);
    char base = *p;
    const uint16_t letter_keys = directions | (layer ? (1u << unsigned(Button::L)) : 0) |
                                 (1u << unsigned(b));
    const uint16_t eligible_keys = _held == letter_keys ? letter_keys : 0;
    if (!_select && b == Button::R && !layer && (dir == 'R' || dir == 'L')) {
      if(!_group_r_count)_group_upper=_caps||_shift;
      ++_group_r_count;
      if (_group_r_count == 2) {
        _group_r_count = 0;
        _letter_keys = eligible_keys;
        _letter_base = dir == 'L' ? 'v' : 'g';
        _letter_upper = _group_upper;
        return {EventKind::REPLACE,
                (_group_upper ? (dir == 'L'?"V":"G") : (dir == 'L'?"v":"g"))};
      }
    }
    if (!_select) {
      _letter_keys = eligible_keys;
      _letter_base = base;
      _letter_upper = _caps || _shift;
    }
    return letter_event(base, _select);
  }
  if (_select) {
    const char *cycle = nullptr;
    int step = 1;
    if (b == Button::UP || b == Button::DOWN) {
      cycle = "1234567890";
      step = b == Button::UP ? 1 : -1;
    } else if (b == Button::RIGHT || b == Button::LEFT) {
      cycle = ".()/;@#%&_+=-";
      step = b == Button::RIGHT ? 1 : -1;
    } else if (!dir && (b == Button::R || b == Button::L)) {
      cycle = ".,'\":!?";
      step = b == Button::R ? 1 : -1;
    }
    if (cycle) {
      int count = std::strlen(cycle);
      const char *found =
          _select_value[1] ? nullptr : std::strchr(cycle, _select_value[0]);
      int index = found ? found - cycle : (step > 0 ? -1 : 0);
      index = (index + step + count) % count;
      _select_value[0] = cycle[index];
      _select_value[1] = 0;
      _alternate_base = 0;
      _select_alpha = false;
      return {EventKind::REPLACE, _select_value};
    }
    return {EventKind::NONE, ""};
  }
  if (b == Button::R && _held == (1u << unsigned(Button::R))) {
    _r_pending = true;
    _r_frames = 0;
    return {EventKind::NONE, ""};
  }
  if (!_select && b == Button::A)
    return {EventKind::INSERT, " "};
  if (!_select && b == Button::B)
    return {EventKind::BACKSPACE, ""};
  return {EventKind::NONE, ""};
}
InputEvent InputState::release(Button b) {
  set(b, false);
  _previous &= ~(1u << unsigned(b));
  if (b == Button::R && _r_pending) {
    _shift = !_shift && !_caps;
    _caps = false;
    _r_pending = false;
  }
  if (_letter_keys & (1u << unsigned(b))) _letter_keys = 0;
  if (b == Button::START) {
    bool used = _start_used;
    _start_used = false;
    return used ? InputEvent{EventKind::NONE, ""}
                : InputEvent{EventKind::INSERT, "\n"};
  }
  if (b == Button::SELECT) {
    if (_select_alpha)
      _shift = false;
    _select_alpha = false;
    _select = false;
    _select_existing = false;
    _alternate_base = 0;
  }
  if (b == Button::UP || b == Button::DOWN || b == Button::LEFT ||
      b == Button::RIGHT || b == Button::L) {
    _group_r_count = 0;
    _alternate_base = 0;
  }
  return {EventKind::NONE, ""};
}
void InputState::update(uint16_t snapshot,Consumer consume,void* context) {
  // Eligibility is continuous exact group + producing button, never a timer.
  if ((snapshot & ~(1u << unsigned(Button::SELECT))) != _letter_keys)
    _letter_keys = 0;
  constexpr uint16_t chord=(1u<<unsigned(Button::START))|(1u<<unsigned(Button::SELECT));
  // Own the entire chord session before releases, typing, repeats or saves.
  if(_toggle_latched){
    if(!(snapshot&chord)){
      reset_transient();_held=snapshot;_previous=snapshot;
    }
    return;
  }
  if((snapshot&chord)==chord){
    consume(context,{EventKind::TOGGLE_STATUS,""});
    reset_transient();_toggle_latched=true;
    return;
  }
  uint16_t released=_previous&~snapshot,pressed=snapshot&~_previous;
  constexpr Button order[]={Button::START,Button::SELECT,Button::L,Button::UP,Button::DOWN,Button::LEFT,Button::RIGHT,Button::B,Button::A,Button::R};
  auto emit=[&](InputEvent e){if(e.kind!=EventKind::NONE)consume(context,e);};
  auto dispatch=[&](Button b,bool up){
    InputState before=*this;_rejected=false;
    emit(up?release(b):press(b,true));
    if(_rejected){
      _shift=before._shift;
      _select_alpha=before._select_alpha;
      _alternate_base=before._alternate_base;_alternate_index=before._alternate_index;
      std::strcpy(_select_value,before._select_value);
    }
  };
  // Release state before fresh presses; then every press sees the full hardware snapshot.
  for(auto b:order)if(released&(1u<<unsigned(b))) {
    dispatch(b,true);
  }
  _held=snapshot;
  if(snapshot & ~(1u<<unsigned(Button::R)))_r_pending=false;
  for(auto b:order)if(pressed&(1u<<unsigned(b))) {
    dispatch(b,false);
  }
  if(snapshot == (1u<<unsigned(Button::R)) && _r_pending && !(pressed&(1u<<unsigned(Button::R))) && !_shift && !_caps &&
     ++_r_frames>=CAPS_HOLD_DELAY){
    _caps=true;_r_pending=false;
  }
  const uint16_t navigation=snapshot & (15u | (1u<<unsigned(Button::L)) | (1u<<unsigned(Button::R)));
  const bool isolated_edit=snapshot==(1u<<unsigned(Button::A)) || snapshot==(1u<<unsigned(Button::B));
  // A chord's release tail must never arm an edit: require a fresh solo press.
  const bool edit_repeat=isolated_edit && ((pressed&snapshot) || _repeat_keys==snapshot);
  const bool navigation_repeat=!_select && (snapshot&(1u<<unsigned(Button::START))) && navigation && !(navigation&(navigation-1));
  const uint16_t repeat_keys=edit_repeat?snapshot:navigation;
  if(edit_repeat || navigation_repeat) {
    if(repeat_keys!=_repeat_keys){_repeat_keys=repeat_keys;_repeat_frames=0;}
    else {
      ++_repeat_frames;
      if(_repeat_frames>=NAV_REPEAT_DELAY && (_repeat_frames-NAV_REPEAT_DELAY)%NAV_REPEAT_INTERVAL==0)
        for(auto b:order)if(repeat_keys&(1u<<unsigned(b)))dispatch(b,false);
      if(_repeat_frames>=NAV_REPEAT_DELAY+NAV_REPEAT_INTERVAL)_repeat_frames=NAV_REPEAT_DELAY;
    }
  } else {_repeat_keys=0;_repeat_frames=0;}
  _previous=snapshot;
}
} // namespace writer
