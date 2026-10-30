/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). C bridge to GBAWriter's unchanged input engine.
 * Text edits follow GBAWriter's Application::consume (provisional Select
 * accents included); multi-line layout events become list navigation. */
#include "writer_core.h"
#include <new>
extern "C" {
#include "writer_glue.h"
}
using namespace writer;
alignas(4) static unsigned char input_mem[sizeof(InputState)],text_mem[sizeof(TextModel)];
static InputState *input;
static TextModel *model;
static bool provisional,provisional_dirty;
static std::size_t provisional_end;
static unsigned flags;
static void consume(void *,InputEvent e){
 using K=EventKind;bool ok=true;
 switch(e.kind){
 case K::TOGGLE_STATUS:
  if(input->select_active()&&provisional&&model->caret_byte()==provisional_end){if(model->backspace())flags|=WRITER_TEXT;if(!provisional_dirty)model->mark_saved();}
  provisional=false;flags|=WRITER_START_SELECT;break;
 case K::INSERT:
  /* GBAWriter types a newline for START alone; a one-line field has none. */
  if(e.text&&e.text[0]=='\n'&&!e.text[1]){flags|=WRITER_START_ALONE;break;}
  if(input->select_active())provisional_dirty=model->dirty();
  ok=model->insert(e.text);if(ok)flags|=WRITER_TEXT;
  if(input->select_active()){provisional=ok;provisional_end=model->caret_byte();}
  break;
 case K::REPLACE:
  if(input->select_active()&&(!provisional||model->caret_byte()!=provisional_end)){input->reject_edit();return;}
  ok=model->replace_before_caret(e.text);if(ok)flags|=WRITER_TEXT;
  if(input->select_active()&&ok)provisional_end=model->caret_byte();
  break;
 case K::BACKSPACE:if(model->backspace())flags|=WRITER_TEXT;break;
 case K::MOVE_LEFT:model->move_left();flags|=WRITER_TEXT;break;
 case K::MOVE_RIGHT:model->move_right();flags|=WRITER_TEXT;break;
 case K::MOVE_UP:flags|=WRITER_UP;break;
 case K::MOVE_DOWN:flags|=WRITER_DOWN;break;
 case K::PAGE_PREV:flags|=WRITER_PAGE_PREV;break;
 case K::PAGE_NEXT:flags|=WRITER_PAGE_NEXT;break;
 case K::SAVE:flags|=WRITER_START_A;break;
 case K::SAVE_MENU:flags|=WRITER_START_B;break;
 default:break;
 }
 if(!ok){input->reject_edit();flags|=WRITER_FULL;}
}
extern "C" void writer_begin(const char *initial){
 if(!input){input=new(input_mem) InputState();model=new(text_mem) TextModel();}
 else{*input=InputState();}
 model->set_text(initial?initial:"");model->move_end();provisional=false;
}
extern "C" unsigned writer_frame(uint16_t g){
 /* GBA: A B SELECT START RIGHT LEFT UP DOWN R L -> Writer: UP DOWN LEFT RIGHT A B L R START SELECT */
 uint16_t h=0;
 if(g&64)h|=1;if(g&128)h|=2;if(g&32)h|=4;if(g&16)h|=8;if(g&1)h|=16;if(g&2)h|=32;if(g&512)h|=64;if(g&256)h|=128;if(g&8)h|=256;if(g&4)h|=512;
 flags=0;input->update(h,consume,nullptr);return flags;
}
extern "C" const char *writer_text(void){return model->data();}
extern "C" unsigned writer_caret(void){return (unsigned)model->caret_byte();}
extern "C" int writer_shift(void){return input->shift_armed();}
extern "C" int writer_caps(void){return input->caps();}
extern "C" const char *writer_group(void){return input->active_group();}
