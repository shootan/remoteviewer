# -*- coding: utf-8 -*-
"""Two gaps the screenshots showed, and one the screenshots cannot show."""
import io

p = 'apps/native_poc/ui/shell.html'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- keyboard focus
old = '''  input:focus { border-color: var(--accent); }'''
new = '''  input:focus { border-color: var(--accent); }
  /* Keyboard focus had no mark of its own: tabbing through the form moved an invisible caret and
     the buttons showed nothing at all. `:focus-visible` leaves mouse clicks alone, so this only
     appears for the people who need it. A screenshot cannot show this -- it is here because the
     common rules ask for it, and it is listed as unphotographed rather than as evidence. */
  input:focus-visible,
  select:focus-visible,
  button:focus-visible {
    outline: 2px solid var(--accent);
    outline-offset: 2px;
  }'''
assert d.count(old) == 1, 'focus anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- sliders start labelled
old = '''  $('bitrate').oninput = labelBitrate;
  $('fps').oninput = labelFps;'''
new = '''  $('bitrate').oninput = labelBitrate;
  $('fps').oninput = labelFps;
  // Labelled from the start, not only once `restore` arrives. Opening settings before that
  // message lands showed two sliders with no numbers beside them -- which is what the preview
  // photographed, and it would be what a user saw if the restore were ever slow or absent.
  labelBitrate();
  labelFps();'''
assert d.count(old) == 1, 'slider init anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('shell ok')

# ---------------------------------------------------------------- photograph the real values
p = 'apps/native_poc/tools/ui_preview.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''      {L"s3-settings.png", "S3 settings",
       [&] {
         gWebView->PostWebMessageAsString(widen(hosts).c_str());
         eval(L"document.getElementById('openSettings').click()");
       }},'''
new = '''      {L"s3-settings.png", "S3 settings",
       [&] {
         // Restored first, the way the client does it: the sliders take their values from this
         // message, and a settings shot without it photographs a screen nobody reaches.
         remote60::native_poc::ShellRuntimeSettings settings;
         settings.bitrateKbps = 12000;
         settings.fps = 60;
         settings.monitorId = 0;
         gWebView->PostWebMessageAsString(
             widen(np::shell_restore_json("https://rem.shotan.net", "shotan", settings)).c_str());
         gWebView->PostWebMessageAsString(widen(hosts).c_str());
         Sleep(200);
         eval(L"document.getElementById('openSettings').click()");
       }},'''
assert d.count(old) == 1, 'preview settings anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('preview ok')
