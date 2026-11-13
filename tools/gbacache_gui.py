#!/usr/bin/env python3
# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
"""Small, bidirectional desktop converter. All Tk work stays on the UI thread."""
import os
from pathlib import Path
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import gbacache
import converter_settings


class ConverterGUI:
    def __init__(self, root):
        self.root = root
        self.busy = False
        self.inputs = ()
        self._folders = set()
        self.events = queue.Queue()
        self.stop_requested = threading.Event()
        self.cancel_requested = threading.Event()
        self._poll_id = self._save_id = None
        self._closed = False
        self.last_result = None
        self.worker = None
        saved = converter_settings.load()
        self.direction = tk.StringVar(root, value=saved['direction'])
        self.output_mode = tk.StringVar(root, value=saved['output_mode'])
        self.output_dir = tk.StringVar(root, value=saved['output_dir'])
        self.recursive = tk.BooleanVar(root, value=saved['recursive'])
        self.rate = tk.IntVar(root, value=saved['rate'])
        self.channels = tk.IntVar(root, value=saved['channels'])
        self.ffmpeg_mode = tk.StringVar(root, value=saved['ffmpeg_mode'])
        self.ffmpeg = tk.StringVar(root, value=saved['ffmpeg_path'])
        self.quality = tk.IntVar(root, value=saved['quality'])
        self.variables = {key: getattr(self, 'ffmpeg' if key == 'ffmpeg_path' else key)
                          for key in converter_settings.DEFAULTS}
        self.status = tk.StringVar(root, value='Add songs or a folder, then press Convert.')
        self.current_song = tk.StringVar(root, value='')
        root.title('GAC Converter')
        root.geometry('740x720')
        root.minsize(380, 300)
        root.protocol('WM_DELETE_WINDOW', self.request_close)
        shell = ttk.Frame(root)
        shell.pack(fill='both', expand=True)
        self.canvas = tk.Canvas(shell, highlightthickness=0)
        scroll = ttk.Scrollbar(shell, orient='vertical', command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=scroll.set)
        scroll.pack(side='right', fill='y')
        self.canvas.pack(side='left', fill='both', expand=True)
        self.panel = panel = ttk.Frame(self.canvas, padding=16)
        self._window = self.canvas.create_window(0, 0, window=panel, anchor='nw')
        panel.bind('<Configure>', lambda event: self.canvas.configure(scrollregion=self.canvas.bbox('all')))
        self.canvas.bind('<Configure>', self._resize)
        root.bind('<MouseWheel>', self._wheel)
        root.bind('<Button-4>', lambda event: self.canvas.yview_scroll(-3, 'units'))
        root.bind('<Button-5>', lambda event: self.canvas.yview_scroll(3, 'units'))
        panel.columnconfigure(0, weight=1)
        heading = ttk.Frame(panel)
        heading.grid(row=0, column=0, sticky='w', pady=(0, 10))
        self.direction_label = ttk.Label(heading, font=('TkDefaultFont', 19, 'bold'))
        self.direction_label.pack(side='left')
        self.direction_button = ttk.Button(heading, text='⇄', width=4, command=self.switch_direction)
        self.direction_button.pack(side='left', padx=12)
        actions = ttk.Frame(panel)
        actions.grid(row=1, column=0, sticky='ew')
        self.files_button = ttk.Button(actions, text='Add songs', command=self.select_files)
        self.folder_button = ttk.Button(actions, text='Add folder', command=self.select_folder)
        self.remove_button = ttk.Button(actions, text='Remove selected', command=self.remove_selected)
        self.clear_button = ttk.Button(actions, text='Clear list', command=self.clear_inputs)
        for index, widget in enumerate((self.files_button, self.folder_button, self.remove_button, self.clear_button)):
            widget.grid(row=index // 2, column=index % 2, sticky='ew', padx=(0, 6), pady=2)
            actions.columnconfigure(index % 2, weight=1)
        listing = ttk.Frame(panel)
        listing.grid(row=2, column=0, sticky='ew', pady=8)
        listing.columnconfigure(0, weight=1)
        self.file_list = tk.Listbox(listing, height=5, selectmode='extended', exportselection=False)
        self.file_list.grid(row=0, column=0, sticky='ew')
        list_y = ttk.Scrollbar(listing, command=self.file_list.yview)
        list_y.grid(row=0, column=1, sticky='ns')
        list_x = ttk.Scrollbar(listing, orient='horizontal', command=self.file_list.xview)
        list_x.grid(row=1, column=0, sticky='ew')
        self.file_list.configure(yscrollcommand=list_y.set, xscrollcommand=list_x.set)
        scope = ttk.Frame(panel)
        scope.grid(row=3, column=0, sticky='w')
        self.scope_radios = self._radios(scope, self.recursive,
                                        [(False, "Don't include subfolders"), (True, 'Include subfolders')])
        self.rates = ttk.LabelFrame(panel, text='Sample rate', padding=6)
        self.rates.grid(row=4, column=0, sticky='ew', pady=8)
        self.rate_radios = self._radios(self.rates, self.rate, [(32768, '32.768 kHz'), (44151, '44.151 kHz')])
        output = ttk.Frame(panel)
        output.grid(row=5, column=0, sticky='ew')
        self.output_radios = self._radios(output, self.output_mode, [('replace', ''), ('beside', ''), ('folder', '')])
        destination = ttk.Frame(panel)
        destination.grid(row=6, column=0, sticky='ew', pady=(3, 8))
        destination.columnconfigure(0, weight=1)
        self.output_entry = ttk.Entry(destination, textvariable=self.output_dir)
        self.output_entry.grid(row=0, column=0, sticky='ew')
        self.output_button = ttk.Button(destination, text='Browse', command=self.select_output)
        self.output_button.grid(row=0, column=1, padx=(6, 0))
        self.advanced_button = ttk.Button(panel, text='▸ Advanced', command=self.toggle_advanced)
        self.advanced_button.grid(row=7, column=0, sticky='w')
        self.advanced = ttk.Frame(panel, padding=(8, 6))
        self.advanced.grid(row=8, column=0, sticky='ew')
        self.advanced.columnconfigure(0, weight=1)
        self.channel_frame = ttk.LabelFrame(self.advanced, text='Channels', padding=4)
        self.channel_frame.grid(row=0, column=0, sticky='ew')
        self.channel_radios = self._radios(self.channel_frame, self.channels, [(2, 'Stereo'), (1, 'Mono')])
        ff = ttk.LabelFrame(self.advanced, text='FFmpeg', padding=4)
        ff.grid(row=1, column=0, sticky='ew', pady=4)
        ff.columnconfigure(0, weight=1)
        ffchoices = ttk.Frame(ff)
        ffchoices.grid(row=0, column=0, columnspan=2, sticky='w')
        self.ffmpeg_radios = self._radios(ffchoices, self.ffmpeg_mode, [('auto', 'Automatic'), ('manual', 'Choose executable')])
        self.ffmpeg_entry = ttk.Entry(ff, textvariable=self.ffmpeg)
        self.ffmpeg_entry.grid(row=1, column=0, sticky='ew')
        self.ffmpeg_button = ttk.Button(ff, text='Browse', command=self.select_ffmpeg)
        self.ffmpeg_button.grid(row=1, column=1, padx=6)
        self.quality_frame = ttk.Frame(self.advanced)
        self.quality_frame.grid(row=2, column=0, sticky='w')
        ttk.Label(self.quality_frame, text='MP3 VBR quality (V0 highest)').pack(side='left')
        self.quality_choice = ttk.Combobox(self.quality_frame, state='readonly', width=4,
                                         values=tuple(str(value) for value in range(10)), textvariable=self.quality)
        self.quality_choice.pack(side='left', padx=8)
        self.advanced.grid_remove()
        run = ttk.Frame(panel)
        run.grid(row=9, column=0, sticky='ew', pady=10)
        self.convert_button = ttk.Button(run, text='Convert', command=self.start_conversion)
        self.convert_button.grid(row=0, column=0, sticky='w')
        self.stop_button = ttk.Button(run, text='Stop after current song', command=self.request_stop)
        self.stop_button.grid(row=0, column=1, sticky='w', padx=8)
        # Long songs take minutes: this one abandons the current song at once.
        self.cancel_button = ttk.Button(run, text='Cancel now', command=self.request_cancel)
        self.cancel_button.grid(row=1, column=1, sticky='w', padx=8, pady=(4, 0))
        self.song_label = ttk.Label(panel, textvariable=self.current_song, wraplength=650)
        self.song_label.grid(row=10, column=0, sticky='w')
        self.progress = ttk.Progressbar(panel, maximum=1)
        self.progress.grid(row=11, column=0, sticky='ew', pady=4)
        self.status_label = ttk.Label(panel, textvariable=self.status, wraplength=650)
        self.status_label.grid(row=12, column=0, sticky='w')
        log_frame = ttk.Frame(panel)
        log_frame.grid(row=13, column=0, sticky='ew', pady=(8, 0))
        log_frame.columnconfigure(0, weight=1)
        self.log = tk.Text(log_frame, height=5, width=30, wrap='word', state='disabled')
        self.log.grid(row=0, column=0, sticky='ew')
        log_scroll = ttk.Scrollbar(log_frame, command=self.log.yview)
        log_scroll.grid(row=0, column=1, sticky='ns')
        self.log.configure(yscrollcommand=log_scroll.set)
        self.controls = [self.direction_button, self.files_button, self.folder_button,
                         self.remove_button, self.clear_button, self.convert_button,
                         *self.scope_radios, *self.rate_radios, *self.output_radios,
                         *self.channel_radios, *self.ffmpeg_radios, self.quality_choice,
                         self.output_entry, self.output_button, self.ffmpeg_entry, self.ffmpeg_button]
        for variable in self.variables.values():
            variable.trace_add('write', self._setting_changed)
        self.update_output_state()

    @staticmethod
    def _radios(parent, variable, choices):
        widgets = []
        for value, label in choices:
            widget = ttk.Radiobutton(parent, text=label, variable=variable, value=value)
            widget.pack(anchor='w', pady=1)
            widgets.append(widget)
        return widgets

    def _resize(self, event):
        self.canvas.itemconfigure(self._window, width=event.width)
        ttk.Style(self.root).configure('Output.TRadiobutton', wraplength=max(160, event.width - 64))
        for radio in self.output_radios:
            radio.configure(style='Output.TRadiobutton')
        for widget in (self.song_label, self.status_label):
            widget.configure(wraplength=max(200, event.width - 40))

    def _wheel(self, event):
        self.canvas.yview_scroll(-1 if event.delta > 0 else 1, 'units')

    def _setting_changed(self, *unused):
        self.update_output_state()
        if self._save_id is not None:
            self.root.after_cancel(self._save_id)
        self._save_id = self.root.after(200, self.save_settings)

    def save_settings(self):
        if self._save_id is not None:
            self.root.after_cancel(self._save_id)
        self._save_id = None
        try:
            converter_settings.save({key: value.get() for key, value in self.variables.items()})
        except (OSError, tk.TclError, ValueError) as error:
            self.fail(f'Could not save settings: {error}')

    def update_output_state(self):
        forward = self.direction.get() == 'mp3_to_gac'
        source, target = ('MP3', 'GAC') if forward else ('GAC', 'MP3')
        self.direction_label.configure(text=f'{source} → {target}')
        for widget, label in zip(self.output_radios, (
                f'Replace {source}s with {target} files',
                f'Keep {source}s and create {target} copies in the same folder',
                f'Keep {source}s and create {target} copies here:')):
            widget.configure(text=label)
        self.rates.grid() if forward else self.rates.grid_remove()
        self.channel_frame.grid() if forward else self.channel_frame.grid_remove()
        self.quality_frame.grid_remove() if forward else self.quality_frame.grid()
        for widget in self.controls:
            widget.state(['disabled'] if self.busy else ['!disabled'])
        self.quality_choice.configure(state='disabled' if self.busy else 'readonly')
        for widget in self.scope_radios:
            widget.state(['disabled'] if self.busy or not self._folders else ['!disabled'])
        for widget in (self.output_entry, self.output_button):
            widget.state(['disabled'] if self.busy or self.output_mode.get() != 'folder' else ['!disabled'])
        for widget in (self.ffmpeg_entry, self.ffmpeg_button):
            widget.state(['disabled'] if self.busy or self.ffmpeg_mode.get() != 'manual' else ['!disabled'])
        self.file_list.configure(state='disabled' if self.busy else 'normal')
        self.stop_button.state(['!disabled'] if self.busy and not self.stop_requested.is_set() else ['disabled'])
        self.cancel_button.state(['!disabled'] if self.busy and not self.cancel_requested.is_set() else ['disabled'])

    def selected_rate(self):
        return self.rate.get()

    def toggle_advanced(self):
        if self.advanced.winfo_manager():
            self.advanced.grid_remove()
            self.advanced_button.configure(text='▸ Advanced')
        else:
            self.advanced.grid()
            self.advanced_button.configure(text='▾ Advanced')

    def switch_direction(self):
        if self.busy:
            return
        self.direction.set('gac_to_mp3' if self.direction.get() == 'mp3_to_gac' else 'mp3_to_gac')
        suffix = '.mp3' if self.direction.get() == 'mp3_to_gac' else '.gac'
        kept = tuple(path for path in self.inputs if path in self._folders or path.suffix.lower() == suffix)
        removed = len(self.inputs) - len(kept)
        self.inputs = kept
        self._refresh_inputs()
        self.status.set(f'Direction changed; removed {removed} incompatible song(s). Folders will be scanned for {suffix} files.')

    def _refresh_inputs(self):
        self.file_list.delete(0, 'end')
        for path in self.inputs:
            self.file_list.insert('end', str(path) + ('  (folder)' if path in self._folders else ''))
        self.update_output_state()

    def add_inputs(self, paths):
        if self.busy:
            return
        suffix = '.mp3' if self.direction.get() == 'mp3_to_gac' else '.gac'
        known = set(self.inputs)
        rejected = 0
        for raw in paths:
            path = Path(raw).expanduser().absolute()
            is_folder = path.is_dir()
            if not is_folder and path.suffix.lower() != suffix:
                rejected += 1
                continue
            if path not in known:
                known.add(path)
                self.inputs += (path,)
                if is_folder:
                    self._folders.add(path)
        self._refresh_inputs()
        if rejected:
            self.status.set(f'Ignored {rejected} incompatible file(s); choose {suffix} songs or folders.')

    def select_files(self):
        ext = 'mp3' if self.direction.get() == 'mp3_to_gac' else 'gac'
        files = filedialog.askopenfilenames(parent=self.root, title=f'Choose {ext.upper()} songs',
                                           filetypes=[(ext.upper(), f'*.{ext} *.{ext.upper()}')])
        if files:
            self.add_inputs(self.root.tk.splitlist(files) if isinstance(files, str) else files)

    def select_folder(self):
        folder = filedialog.askdirectory(parent=self.root, title='Choose a folder')
        if folder:
            self.add_inputs([folder])

    def remove_selected(self):
        if self.busy:
            return
        selected = set(self.file_list.curselection())
        self.inputs = tuple(path for i, path in enumerate(self.inputs) if i not in selected)
        self._folders.intersection_update(self.inputs)
        self._refresh_inputs()

    def clear_inputs(self):
        if not self.busy:
            self.inputs = ()
            self._folders.clear()
            self._refresh_inputs()

    def select_output(self):
        folder = filedialog.askdirectory(parent=self.root, title='Output folder')
        if folder:
            self.output_dir.set(folder)

    def select_ffmpeg(self):
        path = filedialog.askopenfilename(parent=self.root, title='Choose FFmpeg executable')
        if path:
            self.ffmpeg.set(path)

    def append_log(self, text):
        self.log.configure(state='normal')
        self.log.insert('end', text + '\n')
        self.log.see('end')
        self.log.configure(state='disabled')

    def fail(self, error):
        self.status.set(error)
        self.append_log('ERROR: ' + error)

    def start_conversion(self):
        if self.busy:
            return
        self.last_result = None
        if not self.inputs:
            return self.fail('Choose at least one song or folder.')
        if self.output_mode.get() == 'folder' and not self.output_dir.get().strip():
            return self.fail('Choose an output folder with Browse.')
        if self.ffmpeg_mode.get() == 'manual' and not self.ffmpeg.get().strip():
            return self.fail('Choose the FFmpeg executable in Advanced.')
        options = {key: value.get() for key, value in self.variables.items()}
        self.save_settings()
        self.busy = True
        self.stop_requested.clear()
        self.cancel_requested.clear()
        self.update_output_state()
        self.current_song.set('')
        self.status.set('Scanning selected files and folders…')
        self.progress.configure(mode='indeterminate')
        self.progress.start(12)
        self.worker = threading.Thread(target=self.discover, args=(self.inputs, options), daemon=False)
        self.worker.start()
        self._poll_id = self.root.after(30, self.poll_events)

    def discover(self, inputs, options):
        try:
            requested = options['ffmpeg_path'] if options['ffmpeg_mode'] == 'manual' else os.environ.get('FFMPEG', 'ffmpeg')
            ffmpeg = gbacache.find_ffmpeg(requested)
            if not ffmpeg:
                raise ValueError('FFmpeg not found. Choose its executable in Advanced.')
            output = Path(options['output_dir'].strip()).expanduser() if options['output_mode'] == 'folder' else None
            pairs = gbacache.collect(inputs, recursive=options['recursive'], output_dir=output,
                                     direction=options['direction'])
            gbacache.validate_pairs(pairs)
            if not pairs:
                extension = 'MP3' if options['direction'] == 'mp3_to_gac' else 'GAC'
                raise ValueError(f'No {extension} songs found. Check the folder scope or add songs.')
            self.events.put(('ready', (pairs, ffmpeg, options)))
        except Exception as error:
            self.events.put(('error', str(error)))

    def confirm_replace(self, count, direction):
        source, target = ('MP3', 'GAC') if direction == 'mp3_to_gac' else ('GAC', 'MP3')
        return messagebox.askyesno(
            f'Replace {source} files',
            f'{count} {source} file(s) will be deleted only after their {target} files are saved and checked. '
            'Existing destinations are skipped; their sources are kept. This cannot be undone. Continue?',
            parent=self.root)

    def convert_files(self, pairs, ffmpeg, options):
        succeeded = skipped = failed = completed = 0
        for index, (source, destination) in enumerate(pairs, 1):
            if self.stop_requested.is_set():
                break
            self.events.put(('song', (index, len(pairs), source.name)))
            def progress(fraction, message):
                self.events.put(('progress', (max(0., min(1., float(fraction))), str(message))))
            try:
                common = dict(overwrite=False, replace=options['output_mode'] == 'replace', progress=progress,
                              cancel=self.cancel_requested.is_set)
                if options['direction'] == 'mp3_to_gac':
                    note = gbacache.convert(source, destination, ffmpeg, options['rate'],
                                            channels=options['channels'], **common)
                else:
                    note = gbacache.reverse_convert(source, destination, ffmpeg,
                                                    quality=options['quality'], channels=None, **common)
            except gbacache.Cancelled:
                self.events.put(('log', f'Cancelled: {source.name}; no files changed.'))
                break
            except gbacache.Skipped as error:
                skipped += 1
                self.events.put(('log', f'Skipped: {source.name}: {error}'))
            except Exception as error:
                failed += 1
                self.events.put(('log', f'ERROR: {source.name}: {error}'))
            else:
                succeeded += 1
                self.events.put(('log', 'Saved: ' + note))
            completed += 1
            self.events.put(('progress', (1., 'Song finished')))
        self.events.put(('done', dict(succeeded=succeeded, skipped=skipped, failed=failed,
                                      remaining=len(pairs) - completed, stopped=self.stop_requested.is_set())))

    def _idle(self):
        self.busy = False
        self.progress.stop()
        self.progress.configure(mode='determinate', maximum=1)
        self.update_output_state()

    def poll_events(self):
        self._poll_id = None
        try:
            # Bound each pump even when the encoder reports thousands of blocks.
            for unused in range(200):
                kind, value = self.events.get_nowait()
                if kind == 'ready':
                    pairs, ffmpeg, options = value
                    self.progress.stop()
                    self.progress.configure(mode='determinate', maximum=1, value=0)
                    if self.stop_requested.is_set():
                        self._idle()
                        self.status.set('Stopped before conversion; no files changed.')
                    elif options['output_mode'] == 'replace' and not self.confirm_replace(len(pairs), options['direction']):
                        self._idle()
                        self.status.set('Cancelled: no files changed.')
                    else:
                        self.worker = threading.Thread(target=self.convert_files, args=(pairs, ffmpeg, options), daemon=False)
                        self.worker.start()
                elif kind == 'error':
                    self._idle()
                    self.fail(value)
                elif kind == 'song':
                    index, total, name = value
                    self.current_song.set(f'File {index}/{total}: {name}')
                    self.status.set('Starting song…')
                    self.progress.configure(value=0)
                elif kind == 'progress':
                    fraction, message = value
                    self.progress.configure(value=fraction)
                    self.status.set(message + (' — stopping after this song' if self.stop_requested.is_set() else ''))
                elif kind == 'log':
                    self.append_log(value)
                elif kind == 'done':
                    self.last_result = value
                    self._idle()
                    text = ('Stopped' if value['stopped'] else 'Complete') + ': '
                    text += f"{value['succeeded']} succeeded, {value['skipped']} skipped, {value['failed']} failed."
                    if value['remaining']:
                        text += f" {value['remaining']} not started."
                    self.status.set(text)
                    self.append_log(text)
        except queue.Empty:
            pass
        if self.busy or not self.events.empty():
            self._poll_id = self.root.after(30, self.poll_events)

    def request_stop(self):
        self.stop_requested.set()
        self.status.set('Stop requested; the current song will finish safely. No next song will start.')
        self.update_output_state()

    def request_cancel(self):
        """Stop now: the current song is abandoned (no files changed), no next song starts."""
        self.stop_requested.set()
        self.cancel_requested.set()
        self.status.set('Cancelling the current song; no files will be changed.')
        self.update_output_state()

    def request_close(self):
        if self.busy:
            self.request_cancel()
            self.status.set('Cancelling the current song (no files changed). Close again when it has stopped.')
            return
        if self._closed:
            return
        self._closed = True
        for timer in (self._save_id, self._poll_id):
            if timer is not None:
                self.root.after_cancel(timer)
        self.save_settings()
        self.root.destroy()


def main():
    root = tk.Tk()
    ConverterGUI(root)
    root.mainloop()


if __name__ == '__main__':
    # The encoder's worker processes start this program again (spawn): a
    # frozen build must hand them over before anything else runs.
    import multiprocessing
    multiprocessing.freeze_support()
    main()
