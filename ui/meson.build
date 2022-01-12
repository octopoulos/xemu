softmmu_ss.add(pixman)
specific_ss.add(when: ['CONFIG_SOFTMMU'], if_true: pixman)   # for the include path

softmmu_ss.add(files(
  'qemui/clipboard.c',
  'qemui/console.c',
  'qemui/cursor.c',
  'qemui/input-barrier.c',
  'qemui/input-keymap.c',
  'qemui/input-legacy.c',
  'qemui/input.c',
  'qemui/kbd-state.c',
  'qemui/keymaps.c',
  'qemui/qemu-pixman.c',
  'qemui/udmabuf.c',
))

xemu_ss = ss.source_set()
xemu_ss.add(files(
  'imgui/imgui.cpp',
  'imgui/imgui_draw.cpp',
  'imgui/imgui_tables.cpp',
  'imgui/imgui_demo.cpp',
  'imgui/imgui_widgets.cpp',
  'imgui/backends/imgui_impl_opengl3.cpp',
  'imgui/backends/imgui_impl_sdl.cpp',
  'imgui/misc/cpp/imgui_stdlib.cpp',
  'imgui/misc/freetype/imgui_freetype.cpp',
  'implot/implot.cpp',
  'implot/implot_demo.cpp',
  'implot/implot_items.cpp',

  'xemu-custom-widgets.cpp',
  'xemu-hud.cpp',
  'xemu-input.cpp',
  'xemu-monitor.c',
  'xemu-net.cpp',
  'xemu-reporting.cpp',
  'xemu-shaders.cpp',
  'xemu.cpp',

  'extract-xiso.cpp',
  'shuriken.cpp',
  'xsettings.cpp',

  'ui-common.cpp',
  'ui-controls.cpp',
  'ui-file.cpp',
  'ui-games.cpp',
  'ui-log.cpp',
  'ui-settings.cpp',
  'ui-theme.cpp',
))
xemu_ss.add(when: 'CONFIG_WIN32', if_true: files('xemu-update.cpp'))

imgui_flags = ['-DIMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS', '-DIMGUI_USE_STB_SPRINTF', '-DIMGUI_ENABLE_FREETYPE']

if 'CONFIG_WIN32' in config_host
imgui_flags += ['-DWIN32', '-DMINGW32']
endif

if 'CONFIG_LINUX' in config_host
imgui_flags += ['-DLINUX']
endif

if 'CONFIG_DARWIN' in config_host
imgui_flags += ['-DAPPLE']
xemu_cocoa = dependency('appleframeworks', modules: 'Cocoa')
xemu_ss.add(xemu_cocoa) # FIXME: Use existing cocoa name
endif

fmt       = declare_dependency(compile_args: config_host['FMT_CFLAGS'      ].split(), link_args: config_host['FMT_LIBS'      ].split())
freetype2 = declare_dependency(compile_args: config_host['FREETYPE2_CFLAGS'].split(), link_args: config_host['FREETYPE2_LIBS'].split())

imgui = declare_dependency(compile_args: imgui_flags, include_directories: 'imgui')
xemu_ss.add(sdl, opengl, openssl, imgui, fmt, freetype2)
xemu_ss.add(when: 'CONFIG_LINUX', if_true: [shuriken_gtk, files('xemu-os-utils-linux.cpp', 'qemui/noc_file_dialog_gtk.c')])
xemu_ss.add(when: 'CONFIG_WIN32', if_true: files('xemu-os-utils-windows.cpp', 'qemui/noc_file_dialog_win32.c'))
xemu_ss.add(when: 'CONFIG_DARWIN', if_true: files('xemu-os-utils-macos.m', 'qemui/noc_file_dialog_macos.m'))
softmmu_ss.add_all(xemu_ss)

softmmu_ss.add([spice_headers, files('qemui/spice-module.c')])
softmmu_ss.add(when: spice_protocol, if_true: files('qemui/vdagent.c'))

softmmu_ss.add(when: 'CONFIG_LINUX', if_true: files('qemui/input-linux.c'))
softmmu_ss.add(when: cocoa, if_true: files('qemui/cocoa.m'))

vnc_ss = ss.source_set()
vnc_ss.add(files(
  'vnc/vnc-auth-vencrypt.c',
  'vnc/vnc-clipboard.c',
  'vnc/vnc-enc-hextile.c',
  'vnc/vnc-enc-tight.c',
  'vnc/vnc-enc-zlib.c',
  'vnc/vnc-enc-zrle.c',
  'vnc/vnc-jobs.c',
  'vnc/vnc-palette.c',
  'vnc/vnc-ws.c',
  'vnc/vnc.c',
))
vnc_ss.add(zlib, png, jpeg, gnutls)
vnc_ss.add(when: sasl, if_true: files('vnc/vnc-auth-sasl.c'))
softmmu_ss.add_all(when: vnc, if_true: vnc_ss)
softmmu_ss.add(when: vnc, if_false: files('vnc/vnc-stubs.c'))
specific_ss.add(when: ['CONFIG_SOFTMMU'], if_true: opengl)

ui_modules = {}

xemu_enable_extra_ui_modules = false

if xemu_enable_extra_ui_modules

if curses.found()
  curses_ss = ss.source_set()
  curses_ss.add(when: [curses, iconv], if_true: [files('qemui/curses.c'), pixman])
  ui_modules += {'curses' : curses_ss}
endif

if config_host.has_key('CONFIG_OPENGL')
  opengl_ss = ss.source_set()
  opengl_ss.add(gbm)
  opengl_ss.add(when: [opengl, pixman, 'CONFIG_OPENGL'],
               if_true: files('shader.cpp', 'qemui/console-gl.c', 'qemui/egl-helpers.c', 'qemui/egl-context.c'))
  ui_modules += {'opengl' : opengl_ss}
endif

if config_host.has_key('CONFIG_OPENGL') and gbm.found()
  egl_headless_ss = ss.source_set()
  egl_headless_ss.add(when: [opengl, gbm, pixman, 'CONFIG_OPENGL'],
                      if_true: files('qemui/egl-headless.c'))
  ui_modules += {'egl-headless' : egl_headless_ss}
endif

if gtk.found()
  softmmu_ss.add(when: 'CONFIG_WIN32', if_true: files('qemui/win32-kbd-hook.c'))

  gtk_ss = ss.source_set()
  gtk_ss.add(gtk, vte, pixman, files('qemui/gtk.c', 'qemui/gtk-clipboard.c'))
  gtk_ss.add(when: x11, if_true: files('qemui/x_keymap.c'))
  gtk_ss.add(when: [opengl, 'CONFIG_OPENGL'], if_true: files('qemui/gtk-gl-area.c'))
  gtk_ss.add(when: [x11, opengl, 'CONFIG_OPENGL'], if_true: files('qemui/gtk-egl.c'))
  ui_modules += {'gtk' : gtk_ss}
endif

if sdl.found()
  softmmu_ss.add(when: 'CONFIG_WIN32', if_true: files('qemui/win32-kbd-hook.c'))

  sdl_ss = ss.source_set()
  sdl_ss.add(sdl, sdl_image, pixman, glib, files(
    'qemui/sdl2-2d.c',
    'qemui/sdl2-input.c',
    'qemui/sdl2.c',
  ))
  sdl_ss.add(when: [opengl, 'CONFIG_OPENGL'], if_true: files('qemui/sdl2-gl.c'))
  sdl_ss.add(when: x11, if_true: files('qemui/x_keymap.c'))
  ui_modules += {'sdl' : sdl_ss}
endif

if config_host.has_key('CONFIG_SPICE')
  spice_core_ss = ss.source_set()
  spice_core_ss.add(spice, pixman, files(
    'qemui/spice-core.c',
    'qemui/spice-input.c',
    'qemui/spice-display.c'
  ))
  ui_modules += {'spice-core' : spice_core_ss}
endif

if config_host.has_key('CONFIG_SPICE') and config_host.has_key('CONFIG_GIO')
  spice_ss = ss.source_set()
  spice_ss.add(spice, gio, pixman, files('qemui/spice-app.c'))
  ui_modules += {'spice-app': spice_ss}
endif

endif # xemu_enable_extra_ui_modules

keymap_gen = find_program('keycodemapdb/tools/keymap-gen')

keymaps = [
  ['atset1', 'qcode'],
  ['linux', 'qcode'],
  ['qcode', 'atset1'],
  ['qcode', 'atset2'],
  ['qcode', 'atset3'],
  ['qcode', 'linux'],
  ['qcode', 'qnum'],
  ['qcode', 'sun'],
  ['qnum', 'qcode'],
  ['usb', 'qcode'],
  ['win32', 'qcode'],
  ['x11', 'qcode'],
  ['xorgevdev', 'qcode'],
  ['xorgkbd', 'qcode'],
  ['xorgxquartz', 'qcode'],
  ['xorgxwin', 'qcode'],
  ['osx', 'qcode'],
]

if have_system or xkbcommon.found()
  foreach e : keymaps
    output = 'input-keymap-@0@-to-@1@.c.inc'.format(e[0], e[1])
    genh += custom_target(output,
                  output: output,
                  capture: true,
                  input: files('keycodemapdb/data/keymaps.csv'),
                  command: [python.full_path(), files('keycodemapdb/tools/keymap-gen'),
                            'code-map',
                            '--lang', 'glib2',
                            '--varname', 'qemu_input_map_@0@_to_@1@'.format(e[0], e[1]),
                            '@INPUT0@', e[0], e[1]])
  endforeach
endif

subdir('shader')

if have_system
  subdir('icons')

  install_data('qemui/qemu.desktop', install_dir: qemu_desktopdir)
endif

modules += {'ui': ui_modules}
