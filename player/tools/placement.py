"""Local object relocation; SDK archives remain read-only. Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder)."""
from pathlib import Path
import subprocess,json

def apply(build,objects,prefix,layout,run):
 for stem,mappings in layout.get('sections',{}).items():
  matches=[p for p in objects if p.name==stem+'.o'];assert len(matches)==1,(stem,matches)
  obj=matches[0]
  sec={x[1] for l in subprocess.check_output([prefix+'objdump','-h',str(obj)],text=True).splitlines() if len(x:=l.split())>1 and x[0].isdigit()}
  for section,bank in mappings.items():
   assert section in sec,(obj,section)
   assert bank in ['iwram','ewram','ov_mp3','ov_gac']
   run([prefix+'objcopy','--rename-section',section+'=.'+bank+'.placement.'+section.lstrip('.'),obj])
 for flag,archive_name,member,section in [('handles','libsysbase.a','libsysbase_libsysbase_a-handle_manager.o','.data.handles'),('allocator','libc.a','libc_a-mallocr.o','.data.__malloc_av_')]:
  if not layout.get(flag):continue
  archive=subprocess.check_output([prefix+'gcc','-print-file-name='+archive_name],text=True).strip()
  obj=build/(flag+'.o');obj.write_bytes(subprocess.check_output([prefix+'ar','p',archive,member]))
  sec={x[1] for l in subprocess.check_output([prefix+'objdump','-h',str(obj)],text=True).splitlines() if len(x:=l.split())>1 and x[0].isdigit()};assert section in sec
  run([prefix+'objcopy','--rename-section',section+'=.ewram.placement.'+flag,obj]);objects.append(obj)

def overlay(obj,bank,whole_text,prefix,run):
 """Move an object's IWRAM sections (and, when whole_text, all its code) into
 decoder overlay `bank`: .iwram.X -> .ov_mp3.X, .text.X -> .ov_mp3.text.X."""
 sec=[x[1] for l in subprocess.check_output([prefix+'objdump','-h',str(obj)],text=True).splitlines() if len(x:=l.split())>1 and x[0].isdigit()]
 args=[]
 for s in sec:
  if s=='.iwram' or s.startswith('.iwram.'):args+=['--rename-section',s+'=.'+bank+s[len('.iwram'):]]
  elif whole_text and (s=='.text' or s.startswith('.text.')):args+=['--rename-section',s+'=.'+bank+s]
 if args:run([prefix+'objcopy',*args,obj])
